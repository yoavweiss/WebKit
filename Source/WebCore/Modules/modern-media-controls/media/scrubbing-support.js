/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

class ScrubbingSupport extends MediaControllerSupport
{

    // Protected

    get control()
    {
        return this.mediaController.controls.timeControl.scrubber;
    }

    get mediaEvents()
    {
        return ["progress"];
    }

    controlValueWillStartChanging(control)
    {
        const media = this.mediaController.media;
        const isPaused = media.paused;
        if (!isPaused)
            media.pause();

        this._wasPausedWhenScrubbingStarted = isPaused;
    }

    controlValueDidChange(control)
    {
        const media = this.mediaController.media;
        const seekTime = control.value * media.duration;
        this._targetSeekTime = seekTime;
        media.fastSeek(seekTime);
        this.mediaController.controls.timeControl.currentTime = seekTime;
    }

    controlValueDidStopChanging(control)
    {
        const media = this.mediaController.media;
        if (this._targetSeekTime && media.currentTime != this._targetSeekTime)
            media.currentTime = this._targetSeekTime;
        delete this._targetSeekTime;

        if (!this._wasPausedWhenScrubbingStarted)
            this.mediaController.media.play();

        delete this._wasPausedWhenScrubbingStarted;
    }

    syncControl()
    {
        const media = this.mediaController.media;
        if (isNaN(media.duration))
            return;

        // Let's find the buffered time range containing the current time and
        // set the scrubber's secondary value to the end of that range.
        const endTimeForBufferedRangeContainingCurrentTime = () => {
            const ranges = media.buffered;
            const currentTime = media.currentTime;
            for (let i = 0; i < ranges.length; ++i) {
                if (currentTime < ranges.start(i))
                    continue;
                const end = ranges.end(i);
                if (currentTime <= end)
                    return end;
            }
            return 0;
        }

        this.control.secondaryValue = endTimeForBufferedRangeContainingCurrentTime() / media.duration;
    }

}
