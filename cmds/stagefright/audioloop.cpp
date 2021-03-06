/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <utils/String16.h>

#include <binder/ProcessState.h>
#include <media/mediarecorder.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/AMRWriter.h>
#include <media/stagefright/AudioPlayer.h>
#include <media/stagefright/AudioSource.h>
#include <media/stagefright/MediaCodecSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/SimpleDecodingSource.h>
#include "SineSource.h"

using namespace android;

static void usage(const char* name)
{
    fprintf(stderr, "Usage: %s [-d du.ration] [-m] [-w] [<output-file>]\n", name);
    fprintf(stderr, "Encodes either a sine wave or microphone input to AMR format\n");
    fprintf(stderr, "    -d    duration in seconds, default 5 seconds\n");
    fprintf(stderr, "    -m    use microphone for input, default sine source\n");
    fprintf(stderr, "    -w    use AMR wideband (default narrowband)\n");
    fprintf(stderr, "    <output-file> output file for AMR encoding,"
            " if unspecified, decode to speaker.\n");
}

int main(int argc, char* argv[])
{
    static const int channels = 1; // not permitted to be stereo now
    unsigned duration = 5;
    bool useMic = false;
    bool outputWBAMR = false;
    bool playToSpeaker = true;
    const char* fileOut = NULL;
    int ch;
    while ((ch = getopt(argc, argv, "d:mw")) != -1) {
        switch (ch) {
        case 'd':
            duration = atoi(optarg);
            break;
        case 'm':
            useMic = true;
            break;
        case 'w':
            outputWBAMR = true;
            break;
        default:
            usage(argv[0]);
            return -1;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 1) {
        fileOut = argv[0];
    }
    const int32_t kSampleRate = outputWBAMR ? 16000 : 8000;
    const int32_t kBitRate = outputWBAMR ? 16000 : 8000;

    android::ProcessState::self()->startThreadPool();
    sp<MediaSource> source;

    if (useMic) {
        // talk into the appropriate microphone for the duration
        source = new AudioSource(
                AUDIO_SOURCE_MIC,
                String16(),
                kSampleRate,
                channels);
    } else {
        // use a sine source at 500 hz.
        source = new SineSource(kSampleRate, channels);
    }

    sp<AMessage> meta = new AMessage;
    meta->setString(
            "mime",
            outputWBAMR ? MEDIA_MIMETYPE_AUDIO_AMR_WB
                    : MEDIA_MIMETYPE_AUDIO_AMR_NB);

    meta->setInt32("channel-count", channels);
    meta->setInt32("sample-rate", kSampleRate);
    meta->setInt32("bitrate", kBitRate);
    int32_t maxInputSize;
    if (source->getFormat()->findInt32(kKeyMaxInputSize, &maxInputSize)) {
        meta->setInt32("max-input-size", maxInputSize);
    }

    sp<ALooper> looper = new ALooper;
    looper->setName("audioloop");
    looper->start();

    sp<IMediaSource> encoder = MediaCodecSource::Create(looper, meta, source);

    if (fileOut != NULL) {
        // target file specified, write encoded AMR output
        int fd = open(fileOut, O_CREAT | O_LARGEFILE | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            return 1;
        }
        sp<AMRWriter> writer = new AMRWriter(fd);
        close(fd);
        writer->addSource(encoder);
        writer->start();
        sleep(duration);
        writer->stop();
    } else {
        // otherwise decode to speaker
        sp<IMediaSource> decoder = SimpleDecodingSource::Create(encoder);

        if (playToSpeaker) {
            AudioPlayer *player = new AudioPlayer(NULL);
            player->setSource(decoder);
            player->start();
            sleep(duration);

            decoder.clear(); // must clear |decoder| otherwise delete player will hang.
            delete player; // there is no player->stop()...
        } else {
            CHECK_EQ(decoder->start(), (status_t)OK);
            MediaBuffer* buffer;
            while (decoder->read(&buffer) == OK) {
                // do something with buffer (save it eventually?)
                // need to stop after some count though...
                putchar('.');
                fflush(stdout);
                buffer->release();
                buffer = NULL;
            }
            CHECK_EQ(decoder->stop(), (status_t)OK);
        }
    }

    return 0;
}
