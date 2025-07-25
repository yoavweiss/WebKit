/*
 * Copyright (C) 2017-2025 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WebAnimation.h"

#include "AnimationEffect.h"
#include "AnimationPlaybackEvent.h"
#include "AnimationTimeline.h"
#include "ContainerNodeInlines.h"
#include "CSSSerializationContext.h"
#include "CSSStyleProperties.h"
#include "CSSUnitValue.h"
#include "CSSUnits.h"
#include "CSSValuePool.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "DOMPromiseProxy.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "DocumentTimeline.h"
#include "Element.h"
#include "EventLoop.h"
#include "EventNames.h"
#include "HTMLNames.h"
#include "InspectorInstrumentation.h"
#include "JSWebAnimation.h"
#include "KeyframeEffect.h"
#include "KeyframeEffectStack.h"
#include "Logging.h"
#include "RenderElement.h"
#include "StyleExtractor.h"
#include "StyleOriginatedAnimation.h"
#include "StylePropertyShorthand.h"
#include "StyleResolver.h"
#include "StyledElement.h"
#include "WebAnimationTypes.h"
#include "WebAnimationUtilities.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/TextStream.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(WebAnimation);

HashSet<WebAnimation*>& WebAnimation::instances()
{
    static NeverDestroyed<HashSet<WebAnimation*>> instances;
    return instances;
}

Ref<WebAnimation> WebAnimation::create(Document& document, AnimationEffect* effect)
{
    auto result = adoptRef(*new WebAnimation(document));
    result->initialize();
    result->setEffect(effect);
    result->setTimeline(&document.timeline());

    InspectorInstrumentation::didCreateWebAnimation(result.get());

    return result;
}

Ref<WebAnimation> WebAnimation::create(Document& document, AnimationEffect* effect, AnimationTimeline* timeline)
{
    auto result = adoptRef(*new WebAnimation(document));
    result->initialize();
    result->setEffect(effect);
    if (timeline)
        result->setTimeline(timeline);
    else
        AnimationTimeline::updateGlobalPosition(result);

    InspectorInstrumentation::didCreateWebAnimation(result.get());

    return result;
}

void WebAnimation::initialize()
{
    suspendIfNeeded();
    m_readyPromise->resolve(*this);
}

WebAnimation::WebAnimation(Document& document)
    : ActiveDOMObject(document)
    , m_readyPromise(makeUniqueRef<ReadyPromise>(*this, &WebAnimation::readyPromiseResolve))
    , m_finishedPromise(makeUniqueRef<FinishedPromise>(*this, &WebAnimation::finishedPromiseResolve))
{
    instances().add(this);
}

WebAnimation::~WebAnimation()
{
    InspectorInstrumentation::willDestroyWebAnimation(*this);

    ASSERT(instances().contains(this));
    instances().remove(this);
}

void WebAnimation::contextDestroyed()
{
    InspectorInstrumentation::willDestroyWebAnimation(*this);

    ActiveDOMObject::contextDestroyed();
}

void WebAnimation::remove()
{
    // This object could be deleted after either clearing the effect or timeline relationship.
    Ref protectedThis { *this };
    setEffectInternal(nullptr);
    setTimelineInternal(nullptr);
    m_holdTime = std::nullopt;
    m_startTime = std::nullopt;
}

void WebAnimation::suspendEffectInvalidation()
{
    ++m_suspendCount;
}

void WebAnimation::unsuspendEffectInvalidation()
{
    ASSERT(m_suspendCount > 0);
    --m_suspendCount;
}

void WebAnimation::effectTimingDidChange()
{
    timingDidChange(DidSeek::No, SynchronouslyNotify::Yes);

    if (m_effect)
        m_effect->animationDidChangeTimingProperties();

    InspectorInstrumentation::didChangeWebAnimationEffectTiming(*this);
}

void WebAnimation::setId(String&& id)
{
    m_id = WTFMove(id);

    InspectorInstrumentation::didChangeWebAnimationName(*this);
}

void WebAnimation::setBindingsEffect(RefPtr<AnimationEffect>&& newEffect)
{
    setEffect(WTFMove(newEffect));
}

void WebAnimation::setEffect(RefPtr<AnimationEffect>&& newEffect)
{
    // 3.4.3. Setting the target effect of an animation
    // https://drafts.csswg.org/web-animations-1/#setting-the-target-effect

    // 1. Let old effect be the current target effect of animation, if any.
    auto oldEffect = m_effect;

    // 2. If new effect is the same object as old effect, abort this procedure.
    if (newEffect == oldEffect)
        return;

    // 3. If animation has a pending pause task, reschedule that task to run as soon as animation is ready.
    if (hasPendingPauseTask())
        m_timeToRunPendingPauseTask = TimeToRunPendingTask::WhenReady;

    // 4. If animation has a pending play task, reschedule that task to run as soon as animation is ready to play new effect.
    if (hasPendingPlayTask())
        m_timeToRunPendingPlayTask = TimeToRunPendingTask::WhenReady;

    // 5. If new effect is not null and if new effect is the target effect of another animation, previous animation, run the
    // procedure to set the target effect of an animation (this procedure) on previous animation passing null as new effect.
    if (newEffect && newEffect->animation())
        newEffect->animation()->setEffect(nullptr);

    // 6. Let the target effect of animation be new effect.
    // In the case of a style-originated animation, we don't want to remove the animation from the relevant maps because
    // while the effect was set via the API, the element still has a transition or animation set up and we must
    // not break the timeline-to-animation relationship.

    invalidateEffect();

    // This object could be deleted after clearing the effect relationship.
    Ref protectedThis { *this };
    setEffectInternal(WTFMove(newEffect), isStyleOriginatedAnimation());

    // 7. Run the procedure to update an animation's finished state for animation with the did seek flag set to false,
    // and the synchronously notify flag set to false.
    timingDidChange(DidSeek::No, SynchronouslyNotify::No);

    invalidateEffect();
}

void WebAnimation::setEffectInternal(RefPtr<AnimationEffect>&& newEffect, bool doNotRemoveFromTimeline)
{
    if (m_effect == newEffect)
        return;

    RefPtr oldEffect = std::exchange(m_effect, WTFMove(newEffect));

    RefPtr oldKeyframeEffect = dynamicDowncast<KeyframeEffect>(oldEffect);
    std::optional<const Styleable> previousTarget = oldKeyframeEffect ? oldKeyframeEffect->targetStyleable() : std::nullopt;
    RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect);
    std::optional<const Styleable> newTarget = keyframeEffect ? keyframeEffect->targetStyleable() : std::nullopt;

    // Update the effect-to-animation relationships and the timeline's animation map.
    if (oldEffect) {
        oldEffect->setAnimation(nullptr);
        if (!doNotRemoveFromTimeline && previousTarget && previousTarget != newTarget)
            previousTarget->animationWasRemoved(*this);
    }

    if (m_effect) {
        m_effect->setAnimation(this);
        if (newTarget && previousTarget != newTarget)
            newTarget->animationWasAdded(*this);
    }

    InspectorInstrumentation::didSetWebAnimationEffect(*this);
}

void WebAnimation::setBindingsTimeline(RefPtr<AnimationTimeline>&& timeline)
{
    setTimeline(WTFMove(timeline));
}

void WebAnimation::setTimeline(RefPtr<AnimationTimeline>&& timeline)
{
    // 3.4.1. Setting the timeline of an animation
    // https://drafts.csswg.org/web-animations-1/#setting-the-timeline

    // 1. Let old timeline be the current timeline of animation, if any.
    RefPtr oldTimeline = m_timeline;

    // 2. If new timeline is the same object as old timeline, abort this procedure.
    if (timeline == oldTimeline)
        return;

    // 3. Let previous play state be animation’s play state.
    auto previousPlayState = playState();

    // 4. Let previous current time be the animation’s current time.
    auto previousCurrentTime = currentTime();

    // 5. Set previous progress based in the first condition that applies:
    auto previousProgress = [&]() -> std::optional<double> {
        // If previous current time is unresolved: Set previous progress to unresolved.
        if (!previousCurrentTime)
            return std::nullopt;
        // If end time is zero: Set previous progress to zero.
        auto endTime = effectEndTime();
        if (endTime.isZero())
            return 0.0;
        // Otherwise: Set previous progress = previous current time / end time
        return *previousCurrentTime / endTime;
    }();

    // 6. Let from finite timeline be true if old timeline is not null and not monotonically increasing.
    auto fromFiniteTimeline = oldTimeline && !oldTimeline->isMonotonic();

    // 7. Let to finite timeline be true if timeline is not null and not monotonically increasing.
    auto toFiniteTimeline = timeline && !timeline->isMonotonic();

    // 8. Let the timeline of animation be new timeline.
    if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect)) {
        if (auto target = keyframeEffect->targetStyleable()) {
            // In the case of a dstyle-originated animation, we don't want to remove the animation from the relevant maps because
            // while the timeline was set via the API, the element still has a transition or animation set up and we must
            // not break the relationship.
            if (!isStyleOriginatedAnimation())
                target->animationWasRemoved(*this);
            target->animationWasAdded(*this);
        }
    }

    // This object could be deleted after clearing the timeline relationship.
    Ref protectedThis { *this };
    setTimelineInternal(WTFMove(timeline));

    RefPtr documentTimeline = dynamicDowncast<DocumentTimeline>(m_timeline.get());
    setSuspended(documentTimeline && documentTimeline->animationsAreSuspended());

    // 9. Perform the steps corresponding to the first matching condition from the following, if any:
    if (toFiniteTimeline) {
        // If to finite timeline,
        // 1. Apply any pending playback rate on animation.
        applyPendingPlaybackRate();

        // 2. Set auto align start time to true.
        m_autoAlignStartTime = true;

        // 3. Set start time to unresolved.
        m_startTime = std::nullopt;

        // 4. Set hold time to unresolved.
        m_holdTime = std::nullopt;

        if (previousPlayState == PlayState::Finished || previousPlayState == PlayState::Running) {
            // 5. If previous play state is "finished" or "running":
            //    Schedule a pending play task.
            // FIXME: re-creating the ready promise is not part of the spec but Chrome implements this
            // behavior and it makes sense since the new start time won't be computed until the timeline
            // is updated. This is covered by https://github.com/w3c/csswg-drafts/issues/11465.
            auto wasAlreadyPending = pending();
            m_timeToRunPendingPlayTask = TimeToRunPendingTask::WhenReady;
            if (!wasAlreadyPending)
                m_readyPromise = makeUniqueRef<ReadyPromise>(*this, &WebAnimation::readyPromiseResolve);
        } else if (previousPlayState == PlayState::Paused && previousProgress) {
            // 6. If previous play state is "paused" and previous progress is resolved:
            //    Set hold time to previous progress * end time.
            m_holdTime = effectEndTime() * *previousProgress;
        }
    } else if (fromFiniteTimeline && previousProgress) {
        // If from finite timeline and previous progress is resolved,
        // Run the procedure to set the current time to previous progress * end time.
        setCurrentTime(effectEndTime() * *previousProgress);
    }

    // 10. If the start time of animation is resolved, make animation’s hold time unresolved.
    if (m_startTime) {
        // FIXME: we may now be in a state where the hold time and start times have
        // incompatible time units per https://github.com/w3c/csswg-drafts/issues/11761.
        // Until the spec knows how to handle this case, we ensure the start time matches
        // the value type of the currently resolved hold time before we make it unresolved.
        if (m_holdTime && m_holdTime->time().has_value() != m_startTime->time().has_value())
            m_startTime = m_holdTime;
        m_holdTime = std::nullopt;
    }

    // 11. Run the procedure to update an animation's finished state for animation with the did seek flag set to false,
    // and the synchronously notify flag set to false.
    timingDidChange(DidSeek::No, SynchronouslyNotify::No);

    invalidateEffect();
}

void WebAnimation::setTimelineInternal(RefPtr<AnimationTimeline>&& timeline)
{
    if (m_timeline == timeline)
        return;

    if (m_timeline)
        m_timeline->removeAnimation(*this);

    m_timeline = WTFMove(timeline);

    if (m_effect)
        m_effect->animationTimelineDidChange(m_timeline.get());
}

void WebAnimation::effectTargetDidChange(const std::optional<const Styleable>& previousTarget, const std::optional<const Styleable>& newTarget)
{
    if (m_timeline) {
        if (previousTarget)
            previousTarget->animationWasRemoved(*this);

        if (newTarget)
            newTarget->animationWasAdded(*this);

        // This could have changed whether we have replaced animations, so we may need to schedule an update.
        m_timeline->animationTimingDidChange(*this);
    }

    InspectorInstrumentation::didChangeWebAnimationEffectTarget(*this);
}

bool WebAnimation::isTimeValid(const std::optional<WebAnimationTime>& time) const
{
    // https://drafts.csswg.org/web-animations-2/#validating-a-css-numberish-time
    if (time && !time->isValid())
        return false;
    if (m_timeline && m_timeline->isProgressBased() && time && time->time())
        return false;
    if ((!m_timeline || m_timeline->isMonotonic()) && time && time->percentage())
        return false;
    return true;
}

ExceptionOr<void> WebAnimation::setBindingsStartTime(const std::optional<WebAnimationTime>& startTime)
{
    if (!isTimeValid(startTime))
        return Exception { ExceptionCode::TypeError };
    setStartTime(startTime);
    return { };
}

void WebAnimation::setStartTime(std::optional<WebAnimationTime> newStartTime)
{
    // https://drafts.csswg.org/web-animations-2/#setting-the-start-time-of-an-animation

    // 1. Let valid start time be the result of running the validate a CSSNumberish time
    // procedure with new start time as the input.
    // 2. If valid start time is false, abort this procedure.
    // (We do this in setBindingsStartTime())

    // 3. Set auto align start time to false.
    m_autoAlignStartTime = false;

    // 4. Let timeline time be the current time value of the timeline that animation is associated with. If
    //    there is no timeline associated with animation or the associated timeline is inactive, let the timeline
    //    time be unresolved.
    auto timelineTime = m_timeline ? m_timeline->currentTime() : std::nullopt;

    // 5. If timeline time is unresolved and new start time is resolved, make animation's hold time unresolved.
    if (!timelineTime && newStartTime)
        m_holdTime = std::nullopt;

    // 6. Let previous current time be animation's current time.
    auto previousCurrentTime = currentTime();

    // 7. Apply any pending playback rate on animation.
    applyPendingPlaybackRate();

    // 8. Set animation's start time to new start time.
    m_startTime = newStartTime;

    // 9. Update animation's hold time based on the first matching condition from the following,
    if (newStartTime) {
        // If new start time is resolved,
        // If animation's playback rate is not zero, make animation's hold time unresolved.
        if (m_playbackRate)
            m_holdTime = std::nullopt;
    } else {
        // Otherwise (new start time is unresolved),
        // Set animation's hold time to previous current time even if previous current time is unresolved.
        m_holdTime = previousCurrentTime;
    }

    // 10. If animation has a pending play task or a pending pause task, cancel that task and resolve animation's current ready promise with animation.
    if (pending()) {
        m_timeToRunPendingPauseTask = TimeToRunPendingTask::NotScheduled;
        m_timeToRunPendingPlayTask = TimeToRunPendingTask::NotScheduled;
        m_readyPromise->resolve(*this);
    }

    // 11. Run the procedure to update an animation's finished state for animation with the did seek flag set to true, and the synchronously notify flag set to false.
    timingDidChange(DidSeek::Yes, SynchronouslyNotify::No);

    invalidateEffect();
}

ExceptionOr<void> WebAnimation::setBindingsCurrentTime(const std::optional<WebAnimationTime>& currentTime)
{
    if (!isTimeValid(currentTime))
        return Exception { ExceptionCode::TypeError };
    return setCurrentTime(currentTime);
}

std::optional<WebAnimationTime> WebAnimation::currentTime(UseCachedCurrentTime useCachedCurrentTime) const
{
    return currentTime(RespectHoldTime::Yes, useCachedCurrentTime);
}

std::optional<WebAnimationTime> WebAnimation::currentTime(RespectHoldTime respectHoldTime, UseCachedCurrentTime useCachedCurrentTime) const
{
    // 3.4.4. The current time of an animation
    // https://drafts.csswg.org/web-animations-1/#the-current-time-of-an-animation

    // The current time is calculated from the first matching condition from below:

    // If the animation's hold time is resolved, the current time is the animation's hold time.
    if (respectHoldTime == RespectHoldTime::Yes && m_holdTime)
        return m_holdTime;

    // If any of the following are true:
    //     1. the animation has no associated timeline, or
    //     2. the associated timeline is inactive, or
    //     3. the animation's start time is unresolved.
    // The current time is an unresolved time value.
    if (!m_timeline || !m_timeline->currentTime(useCachedCurrentTime) || !m_startTime)
        return std::nullopt;

    // Otherwise, current time = (timeline time - start time) * playback rate
    return (*m_timeline->currentTime(useCachedCurrentTime) - *m_startTime) * m_playbackRate;
}

ExceptionOr<void> WebAnimation::silentlySetCurrentTime(std::optional<WebAnimationTime> seekTime)
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " silentlySetCurrentTime " << seekTime);

    // https://drafts.csswg.org/web-animations-2/#silently-set-the-current-time

    // 1. If seek time is an unresolved time value, then perform the following steps.
    if (!seekTime) {
        // 1. If the current time is resolved, then throw a TypeError.
        if (currentTime())
            return Exception { ExceptionCode::TypeError };
        // 2. Abort these steps.
        return { };
    }

    // 2. Let valid seek time be the result of running the validate a CSSNumberish time procedure
    // with seek time as the input.
    // 3. If valid seek time is false, abort this procedure.
    // (We do this up front in setBindingsCurrentTime()).

    // 4. Set auto align start time to false.
    m_autoAlignStartTime = false;

    // 5. Update either animation's hold time or start time as follows:
    // If any of the following conditions are true:
    //     - animation's hold time is resolved, or
    //     - animation's start time is unresolved, or
    //     - animation has no associated timeline or the associated timeline is inactive, or
    //     - animation's playback rate is 0,
    // Set animation's hold time to seek time.
    // Otherwise, set animation's start time to the result of evaluating timeline time - (seek time / playback rate)
    // where timeline time is the current time value of timeline associated with animation.
    if (m_holdTime || !m_startTime || !m_timeline || !m_timeline->currentTime() || !m_playbackRate)
        m_holdTime = seekTime;
    else
        m_startTime = m_timeline->currentTime().value() - (seekTime.value() / m_playbackRate);

    // 6. If animation has no associated timeline or the associated timeline is inactive, make animation's start time unresolved.
    if (!m_timeline || !m_timeline->currentTime())
        m_startTime = std::nullopt;

    // 7. Make animation's previous current time unresolved.
    m_previousCurrentTime = std::nullopt;

    return { };
}

ExceptionOr<void> WebAnimation::setCurrentTime(std::optional<WebAnimationTime> seekTime)
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " setCurrentTime " << seekTime);

    // Setting the current time of an animation
    // https://drafts.csswg.org/web-animations-2/#setting-the-current-time-of-an-animation

    // 1. Run the steps to silently set the current time of animation to seek time.
    auto silentResult = silentlySetCurrentTime(seekTime);
    if (silentResult.hasException())
        return silentResult.releaseException();

    // 2. If animation has a pending pause task, synchronously complete the pause operation by performing the following steps:
    if (hasPendingPauseTask()) {
        // 1. Set animation's hold time to seek time.
        m_holdTime = seekTime;
        // 2. Apply any pending playback rate to animation.
        applyPendingPlaybackRate();
        // 3. Make animation's start time unresolved.
        m_startTime = std::nullopt;
        // 4. Cancel the pending pause task.
        m_timeToRunPendingPauseTask = TimeToRunPendingTask::NotScheduled;
        // 5. Resolve animation's current ready promise with animation.
        m_readyPromise->resolve(*this);
    }

    // 3. Run the procedure to update an animation's finished state for animation with the did seek flag set to true, and the synchronously notify flag set to false.
    timingDidChange(DidSeek::Yes, SynchronouslyNotify::No);

    if (m_effect)
        m_effect->animationDidChangeTimingProperties();

    invalidateEffect();

    return { };
}

double WebAnimation::effectivePlaybackRate() const
{
    // https://drafts.csswg.org/web-animations/#effective-playback-rate
    // The effective playback rate of an animation is its pending playback rate, if set, otherwise it is the animation's playback rate.
    return m_pendingPlaybackRate ? m_pendingPlaybackRate.value() : m_playbackRate;
}

void WebAnimation::setPlaybackRate(double newPlaybackRate)
{
    // Setting the playback rate of an animation
    // https://drafts.csswg.org/web-animations-1/#setting-the-playback-rate-of-an-animation

    // 1. Clear any pending playback rate on animation.
    m_pendingPlaybackRate = std::nullopt;
    
    // 2. Let previous time be the value of the current time of animation before changing the playback rate.
    auto previousTime = currentTime();

    // 3. Let previous playback rate be the current effective playback rate of animation.
    auto previousPlaybackRate = effectivePlaybackRate();

    // 4. Set the playback rate to new playback rate.
    m_playbackRate = newPlaybackRate;

    // 5. Perform the steps corresponding to the first matching condition from the following, if any:
    if (m_timeline && m_timeline->isMonotonic() && previousTime) {
        // If animation is associated with a monotonically increasing timeline and the previous time is resolved,
        // Set the current time of animation to previous time.
        setCurrentTime(previousTime);
    } else if (m_timeline && !m_timeline->isMonotonic() && m_startTime && !effectEndTime().isInfinity()
        && ((previousPlaybackRate < 0 && m_playbackRate >= 0) || (previousPlaybackRate >= 0 && m_playbackRate < 0))) {
        // If animation is associated with a non-null timeline that is not monotonically increasing,
        // the start time of animation is resolved, associated effect end is not infinity, and either:
        // - the previous playback rate < 0 and the new playback rate ≥ 0, or
        // - the previous playback rate ≥ 0 and the new playback rate < 0,
        // Set animation's start time to the result of evaluating associated effect end − start time for animation.
        m_startTime = effectEndTime() - *m_startTime;
    }

    if (m_effect) {
        m_effect->animationDidChangeTimingProperties();
        m_effect->animationPlaybackRateDidChange();
    }
}

void WebAnimation::updatePlaybackRate(double newPlaybackRate)
{
    // https://drafts.csswg.org/web-animations/#seamlessly-update-the-playback-rate

    // The procedure to seamlessly update the playback rate an animation, animation, to new playback rate preserving its current time is as follows:

    // 1. Let previous play state be animation's play state.
    //    Note: It is necessary to record the play state before updating animation's effective playback rate since, in the following logic,
    //    we want to immediately apply the pending playback rate of animation if it is currently finished regardless of whether or not it will
    //    still be finished after we apply the pending playback rate.
    auto previousPlayState = playState();

    // 2. Let animation's pending playback rate be new playback rate.
    m_pendingPlaybackRate = newPlaybackRate;

    // 3. Perform the steps corresponding to the first matching condition from below:
    if (pending()) {
        // If animation has a pending play task or a pending pause task,
        // Abort these steps.
        // Note: The different types of pending tasks will apply the pending playback rate when they run so there is no further action required in this case.
        return;
    }

    if (previousPlayState == PlayState::Idle || previousPlayState == PlayState::Paused || !currentTime()) {
        // If previous play state is idle or paused, or animation's current time is unresolved,
        // Apply any pending playback rate on animation.
        applyPendingPlaybackRate();
    } else if (previousPlayState == PlayState::Finished) {
        // If previous play state is finished,
        // 1. Let the unconstrained current time be the result of calculating the current time of animation substituting an unresolved time value for the hold time.
        auto unconstrainedCurrentTime = currentTime(RespectHoldTime::No);
        // 2. Let animation's start time be the result of evaluating the following expression:
        // timeline time - (unconstrained current time / pending playback rate)
        // Where timeline time is the current time value of the timeline associated with animation.
        // If pending playback rate is zero, let animation's start time be timeline time.
        
        // If timeline is inactive abort these steps.
        if (!m_timeline->currentTime())
            return;
        
        auto newStartTime = m_timeline->currentTime().value();
        if (m_pendingPlaybackRate)
            newStartTime -= (unconstrainedCurrentTime.value() / m_pendingPlaybackRate.value());
        m_startTime = newStartTime;
        // 3. Apply any pending playback rate on animation.
        applyPendingPlaybackRate();
        // 4. Run the procedure to update an animation's finished state for animation with the did seek flag set to false, and the synchronously notify flag set to false.
        timingDidChange(DidSeek::No, SynchronouslyNotify::No);

        invalidateEffect();
    } else {
        // Otherwise,
        // Run the procedure to play an animation for animation with the auto-rewind flag set to false.
        play(AutoRewind::No);
    }

    if (m_effect)
        m_effect->animationDidChangeTimingProperties();
}

void WebAnimation::applyPendingPlaybackRate()
{
    // https://drafts.csswg.org/web-animations/#apply-any-pending-playback-rate

    // 1. If animation does not have a pending playback rate, abort these steps.
    if (!m_pendingPlaybackRate)
        return;

    // 2. Set animation's playback rate to its pending playback rate.
    m_playbackRate = m_pendingPlaybackRate.value();

    // 3. Clear animation's pending playback rate.
    m_pendingPlaybackRate = std::nullopt;

    if (m_effect)
        m_effect->animationPlaybackRateDidChange();
}

void WebAnimation::setBindingsFrameRate(Variant<FramesPerSecond, AnimationFrameRatePreset>&& frameRate)
{
    m_bindingsFrameRate = WTFMove(frameRate);

    if (std::holds_alternative<FramesPerSecond>(m_bindingsFrameRate)) {
        setEffectiveFrameRate(std::get<FramesPerSecond>(m_bindingsFrameRate));
        return;
    }

    switch (std::get<AnimationFrameRatePreset>(m_bindingsFrameRate)) {
    case AnimationFrameRatePreset::Auto:
        setEffectiveFrameRate(std::nullopt);
        break;
    case AnimationFrameRatePreset::High:
        setEffectiveFrameRate(AnimationFrameRatePresetHigh);
        break;
    case AnimationFrameRatePreset::Low:
        setEffectiveFrameRate(AnimationFrameRatePresetLow);
        break;
    case AnimationFrameRatePreset::Highest:
        setEffectiveFrameRate(std::numeric_limits<FramesPerSecond>::max());
        break;
    }
}

void WebAnimation::setEffectiveFrameRate(std::optional<FramesPerSecond> effectiveFrameRate)
{
    if (m_effectiveFrameRate == effectiveFrameRate)
        return;

    std::optional<FramesPerSecond> maximumFrameRate = std::nullopt;
    if (RefPtr timeline = dynamicDowncast<DocumentTimeline>(m_timeline.get()))
        maximumFrameRate = timeline->maximumFrameRate();

    std::optional<FramesPerSecond> adjustedEffectiveFrameRate;
    if (maximumFrameRate && effectiveFrameRate)
        adjustedEffectiveFrameRate = std::min<FramesPerSecond>(*maximumFrameRate, *effectiveFrameRate);

    if (adjustedEffectiveFrameRate && !*adjustedEffectiveFrameRate)
        adjustedEffectiveFrameRate = std::nullopt;

    if (m_effectiveFrameRate == adjustedEffectiveFrameRate)
        return;

    m_effectiveFrameRate = adjustedEffectiveFrameRate;

    // FIXME: When the effective frame rate of an animation changes, this could have implications
    // on the time until the next animation update is scheduled. We should notify the timeline such
    // that it may schedule an update if our update cadence is now longer (or shorter).
}

auto WebAnimation::playState() const -> PlayState
{
    // 3.5.19 Play states
    // https://drafts.csswg.org/web-animations/#play-states

    // The play state of animation, animation, at a given moment is the state corresponding to the
    // first matching condition from the following:

    // The current time of animation is unresolved, and the start time of animation is unresolved, and
    // animation does not have either a pending play task or a pending pause task,
    // → idle
    auto animationCurrentTime = currentTime();
    if (!animationCurrentTime && !m_startTime && !pending())
        return PlayState::Idle;

    // Animation has a pending pause task, or both the start time of animation is unresolved and it does not
    // have a pending play task,
    // → paused
    if (hasPendingPauseTask() || (!m_startTime && !hasPendingPlayTask()))
        return PlayState::Paused;

    // For animation, current time is resolved and either of the following conditions are true:
    // animation's effective playback rate > 0 and current time ≥ target effect end; or
    // animation's effective playback rate < 0 and current time ≤ 0,
    // → finished
    if (animationCurrentTime && ((effectivePlaybackRate() > 0 && (*animationCurrentTime + animationCurrentTime->matchingEpsilon()) >= effectEndTime()) || (effectivePlaybackRate() < 0 && (*animationCurrentTime - animationCurrentTime->matchingEpsilon()) <= animationCurrentTime->matchingZero())))
        return PlayState::Finished;

    // Otherwise → running
    return PlayState::Running;
}

WebAnimationTime WebAnimation::zeroTime() const
{
    if ((m_timeline && m_timeline->isProgressBased()) || (m_startTime && m_startTime->percentage()) || (m_holdTime && m_holdTime->percentage()))
        return WebAnimationTime::fromPercentage(0);
    return { 0_s };
}

WebAnimationTime WebAnimation::effectEndTime() const
{
    // The target effect end of an animation is equal to the end time of the animation's target effect.
    // If the animation has no target effect, the target effect end is zero.
    return m_effect ? m_effect->endTime() : zeroTime();
}

void WebAnimation::cancel(Silently silently)
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " cancel() (current time is " << currentTime() << ")");

    // 3.4.16. Canceling an animation
    // https://drafts.csswg.org/web-animations-1/#canceling-an-animation-section
    //
    // An animation can be canceled which causes the current time to become unresolved hence removing any effects caused by the target effect.
    //
    // The procedure to cancel an animation for animation is as follows:
    //
    // 1. If animation's play state is not idle, perform the following steps:
    if (playState() != PlayState::Idle) {
        // 1. Run the procedure to reset an animation's pending tasks on animation.
        resetPendingTasks();

        // 2. Reject the current finished promise with a DOMException named "AbortError".
        // 3. Set the [[PromiseIsHandled]] internal slot of the current finished promise to true.
        if (RefPtr context = scriptExecutionContext(); context && !m_finishedPromise->isFulfilled()) {
            context->eventLoop().queueMicrotask([finishedPromise = WTFMove(m_finishedPromise)]() mutable {
                finishedPromise->reject(Exception { ExceptionCode::AbortError }, RejectAsHandled::Yes);
            });
        }

        // 4. Let current finished promise be a new (pending) Promise object.
        m_finishedPromise = makeUniqueRef<FinishedPromise>(*this, &WebAnimation::finishedPromiseResolve);

        // 5. Create an AnimationPlaybackEvent, cancelEvent.
        // 6. Set cancelEvent's type attribute to cancel.
        // 7. Set cancelEvent's currentTime to null.
        // 8. Let timeline time be the current time of the timeline with which animation is associated. If animation is not associated with an
        //    active timeline, let timeline time be n unresolved time value.
        // 9. Set cancelEvent's timelineTime to timeline time. If timeline time is unresolved, set it to null.
        // 10. If animation has a document for timing, then append cancelEvent to its document for timing's pending animation event queue along
        //    with its target, animation. If animation is associated with an active timeline that defines a procedure to convert timeline times
        //    to origin-relative time, let the scheduled event time be the result of applying that procedure to timeline time. Otherwise, the
        //    scheduled event time is an unresolved time value.
        // Otherwise, queue a task to dispatch cancelEvent at animation. The task source for this task is the DOM manipulation task source.
        auto scheduledTime = [&]() -> std::optional<WebAnimationTime> {
            if (RefPtr documentTimeline = dynamicDowncast<DocumentTimeline>(m_timeline.get())) {
                if (auto currentTime = documentTimeline->currentTime())
                    return documentTimeline->convertTimelineTimeToOriginRelativeTime(*currentTime);
            }
            return std::nullopt;
        }();
        enqueueAnimationPlaybackEvent(eventNames().cancelEvent, std::nullopt, scheduledTime);
    }

    // 2. Make animation's hold time unresolved.
    m_holdTime = std::nullopt;

    // 3. Make animation's start time unresolved.
    m_startTime = std::nullopt;

    timingDidChange(DidSeek::No, SynchronouslyNotify::No, silently);

    invalidateEffect();

    if (m_effect)
        m_effect->animationWasCanceled();
}

void WebAnimation::willChangeRenderer()
{
    if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect))
        keyframeEffect->willChangeRenderer();
}

void WebAnimation::enqueueAnimationPlaybackEvent(const AtomString& type, std::optional<WebAnimationTime> currentTime, std::optional<WebAnimationTime> scheduledTime)
{
    auto timelineTime = m_timeline ? m_timeline->currentTime() : std::nullopt;
    auto event = AnimationPlaybackEvent::create(type, this, scheduledTime, timelineTime, currentTime);
    event->setTarget(Ref { *this });
    enqueueAnimationEvent(WTFMove(event));
}

void WebAnimation::enqueueAnimationEvent(Ref<AnimationEventBase>&& event)
{
    auto documentTimeline = [&]() -> DocumentTimeline* {
        if (auto* timeline = dynamicDowncast<DocumentTimeline>(m_timeline.get()))
            return timeline;
        if (RefPtr scrollTimeline = dynamicDowncast<ScrollTimeline>(m_timeline.get())) {
            if (RefPtr source = scrollTimeline->source())
                return Ref { source->document() }->existingTimeline();
        }
        if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect)) {
            if (RefPtr target = keyframeEffect->target())
                return target->protectedDocument()->existingTimeline();
        }
        return nullptr;
    };

    if (RefPtr timeline = documentTimeline()) {
        // If animation has a document for timing, then append event to its document for timing's pending animation event queue along
        // with its target, animation. If animation is associated with an active timeline that defines a procedure to convert timeline times
        // to origin-relative time, let the scheduled event time be the result of applying that procedure to timeline time. Otherwise, the
        // scheduled event time is an unresolved time value.
        m_hasScheduledEventsDuringTick = true;
        timeline->enqueueAnimationEvent(WTFMove(event));
    } else {
        // Otherwise, queue a task to dispatch event at animation. The task source for this task is the DOM manipulation task source.
        if (event->isCSSAnimationEvent() || event->isCSSTransitionEvent()) {
            if (RefPtr element = dynamicDowncast<Element>(event->target())) {
                element->queueTaskToDispatchEvent(TaskSource::DOMManipulation, WTFMove(event));
                return;
            }
        }
        queueTaskToDispatchEvent(*this, TaskSource::DOMManipulation, WTFMove(event));
    }
}

void WebAnimation::animationDidFinish()
{
    if (m_effect)
        m_effect->animationDidFinish();
}

void WebAnimation::resetPendingTasks()
{
    // The procedure to reset an animation's pending tasks for animation is as follows:
    // https://drafts.csswg.org/web-animations-1/#reset-an-animations-pending-tasks
    //
    // 1. If animation does not have a pending play task or a pending pause task, abort this procedure.
    if (!pending())
        return;

    // 2. If animation has a pending play task, cancel that task.
    if (hasPendingPlayTask())
        m_timeToRunPendingPlayTask = TimeToRunPendingTask::NotScheduled;

    // 3. If animation has a pending pause task, cancel that task.
    if (hasPendingPauseTask())
        m_timeToRunPendingPauseTask = TimeToRunPendingTask::NotScheduled;

    // 4. Apply any pending playback rate on animation.
    applyPendingPlaybackRate();

    // 5. Reject animation's current ready promise with a DOMException named "AbortError".
    // 6. Set the [[PromiseIsHandled]] internal slot of animation’s current ready promise to true.
    if (RefPtr context = scriptExecutionContext()) {
        context->eventLoop().queueMicrotask([readyPromise = WTFMove(m_readyPromise)]() mutable {
            if (!readyPromise->isFulfilled())
                readyPromise->reject(Exception { ExceptionCode::AbortError }, RejectAsHandled::Yes);
        });
    }

    // 7. Let animation's current ready promise be the result of creating a new resolved Promise object.
    m_readyPromise = makeUniqueRef<ReadyPromise>(*this, &WebAnimation::readyPromiseResolve);
    m_readyPromise->resolve(*this);
}

ExceptionOr<void> WebAnimation::finish()
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " finish (current time is " << currentTime() << ")");

    // 3.4.15. Finishing an animation
    // https://drafts.csswg.org/web-animations-1/#finishing-an-animation-section

    // An animation can be advanced to the natural end of its current playback direction by using the procedure to finish an animation for animation defined below:
    //
    // 1. If animation's effective playback rate is zero, or if animation's effective playback rate > 0 and target effect end is infinity, throw an InvalidStateError and abort these steps.
    if (!effectivePlaybackRate() || (effectivePlaybackRate() > 0 && effectEndTime().isInfinity()))
        return Exception { ExceptionCode::InvalidStateError };

    // 2. Apply any pending playback rate to animation.
    applyPendingPlaybackRate();

    // 3. Set limit as follows:
    // If animation playback rate > 0, let limit be target effect end.
    // Otherwise, let limit be zero.
    auto limit = m_playbackRate > 0 ? effectEndTime() : zeroTime();

    // 4. Silently set the current time to limit.
    silentlySetCurrentTime(limit);

    // 5. If animation's start time is unresolved and animation has an associated active timeline, let the start time be the result of
    //    evaluating timeline time - (limit / playback rate) where timeline time is the current time value of the associated timeline.
    if (!m_startTime && m_timeline && m_timeline->currentTime())
        m_startTime = m_timeline->currentTime().value() - (limit / m_playbackRate);

    // 6. If there is a pending pause task and start time is resolved,
    if (hasPendingPauseTask() && m_startTime) {
        // 1. Let the hold time be unresolved.
        m_holdTime = std::nullopt;
        // 2. Cancel the pending pause task.
        m_timeToRunPendingPauseTask = TimeToRunPendingTask::NotScheduled;
        // 3. Resolve the current ready promise of animation with animation.
        m_readyPromise->resolve(*this);
    }

    // 7. If there is a pending play task and start time is resolved, cancel that task and resolve the current ready promise of animation with animation.
    if (hasPendingPlayTask() && m_startTime) {
        m_timeToRunPendingPlayTask = TimeToRunPendingTask::NotScheduled;
        m_readyPromise->resolve(*this);
    }

    // 8. Run the procedure to update an animation's finished state animation with the did seek flag set to true, and the synchronously notify flag set to true.
    timingDidChange(DidSeek::Yes, SynchronouslyNotify::Yes);

    invalidateEffect();

    return { };
}

void WebAnimation::timingDidChange(DidSeek didSeek, SynchronouslyNotify synchronouslyNotify, Silently silently)
{
    m_shouldSkipUpdatingFinishedStateWhenResolving = false;
    updateFinishedState(didSeek, synchronouslyNotify);

    if (silently == Silently::No && m_timeline)
        m_timeline->animationTimingDidChange(*this);
};

void WebAnimation::invalidateEffect()
{
    if (isEffectInvalidationSuspended())
        return;

    if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect))
        keyframeEffect->invalidate();
}

void WebAnimation::updateFinishedState(DidSeek didSeek, SynchronouslyNotify synchronouslyNotify)
{
    // 3.4.14. Updating the finished state
    // https://drafts.csswg.org/web-animations-1/#updating-the-finished-state

    // 1. Let the unconstrained current time be the result of calculating the current time substituting an unresolved time value
    // for the hold time if did seek is false. If did seek is true, the unconstrained current time is equal to the current time.
    auto unconstrainedCurrentTime = currentTime(didSeek == DidSeek::Yes ? RespectHoldTime::Yes : RespectHoldTime::No);
    auto endTime = effectEndTime();

    // 2. If all three of the following conditions are true,
    //    - the unconstrained current time is resolved, and
    //    - animation's start time is resolved, and
    //    - animation does not have a pending play task or a pending pause task,
    if (unconstrainedCurrentTime && m_startTime && !pending()) {
        // then update animation's hold time based on the first matching condition for animation from below, if any:
        if (m_playbackRate > 0 && *unconstrainedCurrentTime >= endTime) {
            // If animation playback rate > 0 and unconstrained current time is greater than or equal to target effect end,
            // If did seek is true, let the hold time be the value of unconstrained current time.
            if (didSeek == DidSeek::Yes)
                m_holdTime = unconstrainedCurrentTime;
            // If did seek is false, let the hold time be the maximum value of previous current time and target effect end. If the previous current time is unresolved, let the hold time be target effect end.
            else if (!m_previousCurrentTime)
                m_holdTime = endTime;
            else
                m_holdTime = std::max(m_previousCurrentTime.value(), endTime);
        } else if (m_playbackRate < 0 && *unconstrainedCurrentTime <= unconstrainedCurrentTime->matchingZero()) {
            // If animation playback rate < 0 and unconstrained current time is less than or equal to 0,
            // If did seek is true, let the hold time be the value of unconstrained current time.
            if (didSeek == DidSeek::Yes)
                m_holdTime = unconstrainedCurrentTime;
            // If did seek is false, let the hold time be the minimum value of previous current time and zero. If the previous current time is unresolved, let the hold time be zero.
            else if (!m_previousCurrentTime)
                m_holdTime = zeroTime();
            else
                m_holdTime = std::min(*m_previousCurrentTime, m_previousCurrentTime->matchingZero());
        } else if (m_playbackRate && m_timeline && m_timeline->currentTime()) {
            // If animation playback rate ≠ 0, and animation is associated with an active timeline,
            // Perform the following steps:
            // 1. If did seek is true and the hold time is resolved, let animation's start time be equal to the result of evaluating timeline time - (hold time / playback rate)
            //    where timeline time is the current time value of timeline associated with animation.
            if (didSeek == DidSeek::Yes && m_holdTime)
                m_startTime = m_timeline->currentTime().value() - (m_holdTime.value() / m_playbackRate);
            // 2. Let the hold time be unresolved.
            m_holdTime = std::nullopt;
        }
    }

    // 3. Set the previous current time of animation be the result of calculating its current time.
    m_previousCurrentTime = currentTime();

    // 4. Let current finished state be true if the play state of animation is finished. Otherwise, let it be false.
    auto currentFinishedState = playState() == PlayState::Finished;

    // 5. If current finished state is true and the current finished promise is not yet resolved, perform the following steps:
    if (currentFinishedState && !m_finishedPromise->isFulfilled()) {
        animationDidFinish();
        if (synchronouslyNotify == SynchronouslyNotify::Yes) {
            // If synchronously notify is true, cancel any queued microtask to run the finish notification steps for this animation,
            // and run the finish notification steps immediately.
            m_finishNotificationStepsMicrotaskPending = false;
            finishNotificationSteps();
        } else if (!m_finishNotificationStepsMicrotaskPending) {
            // Otherwise, if synchronously notify is false, queue a microtask to run finish notification steps for animation unless there
            // is already a microtask queued to run those steps for animation.
            m_finishNotificationStepsMicrotaskPending = true;
            if (RefPtr context = scriptExecutionContext()) {
                context->eventLoop().queueMicrotask([this, protectedThis = Ref { *this }] {
                    if (m_finishNotificationStepsMicrotaskPending) {
                        m_finishNotificationStepsMicrotaskPending = false;
                        finishNotificationSteps();
                    }
                });
            }
        }
    }

    // 6. If current finished state is false and animation's current finished promise is already resolved, set animation's current
    // finished promise to a new (pending) Promise object.
    if (!currentFinishedState && m_finishedPromise->isFulfilled())
        m_finishedPromise = makeUniqueRef<FinishedPromise>(*this, &WebAnimation::finishedPromiseResolve);

    updateRelevance();
}

void WebAnimation::finishNotificationSteps()
{
    // 3.4.14. Updating the finished state
    // https://drafts.csswg.org/web-animations-1/#finish-notification-steps

    // Let finish notification steps refer to the following procedure:
    // 1. If animation's play state is not equal to finished, abort these steps.
    if (playState() != PlayState::Finished)
        return;

    // 2. Resolve animation's current finished promise object with animation.
    m_finishedPromise->resolve(*this);

    // 3. Create an AnimationPlaybackEvent, finishEvent.
    // 4. Set finishEvent's type attribute to finish.
    // 5. Set finishEvent's currentTime attribute to the current time of animation.
    // 6. Set finishEvent's timelineTime attribute to the current time of the timeline with which animation is associated.
    //    If animation is not associated with a timeline, or the timeline is inactive, let timelineTime be null.
    // 7. If animation has a document for timing, then append finishEvent to its document for timing's pending animation event
    //    queue along with its target, animation. For the scheduled event time, use the result of converting animation's target
    //    effect end to an origin-relative time.
    //    Otherwise, queue a task to dispatch finishEvent at animation. The task source for this task is the DOM manipulation task source.
    auto scheduledTime = [&]() -> std::optional<WebAnimationTime> {
        if (RefPtr documentTimeline = dynamicDowncast<DocumentTimeline>(m_timeline.get())) {
            if (auto animationEndTime = convertAnimationTimeToTimelineTime(effectEndTime()))
                return documentTimeline->convertTimelineTimeToOriginRelativeTime(*animationEndTime);
        }
        return std::nullopt;
    }();
    enqueueAnimationPlaybackEvent(eventNames().finishEvent, currentTime(), scheduledTime);

    if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect)) {
        if (RefPtr target = keyframeEffect->target()) {
            if (RefPtr page = target->document().page())
                page->chrome().client().animationDidFinishForElement(*target);
        }
    }
}

ExceptionOr<void> WebAnimation::play()
{
    return play(AutoRewind::Yes);
}

ExceptionOr<void> WebAnimation::play(AutoRewind autoRewind)
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " play(autoRewind " << (autoRewind == AutoRewind::Yes) << ") (current time is " << currentTime() << ")");

    // Playing an animation
    // https://drafts.csswg.org/web-animations-2/#playing-an-animation-section

    auto playbackRate = effectivePlaybackRate();
    auto endTime = effectEndTime();

    // 1. Let aborted pause be a boolean flag that is true if animation has a pending pause task, and false otherwise.
    bool abortedPause = hasPendingPauseTask();

    // 2. Let has pending ready promise be a boolean flag that is initially false.
    bool hasPendingReadyPromise = false;

    // 3. Let has finite timeline be true if animation has an associated timeline that is not monotonically increasing.
    auto hasFiniteTimeline = m_timeline && !m_timeline->isMonotonic();

    // 4. Let previous current time be the animation’s current time
    auto previousCurrentTime = currentTime();

    // 5. Let enable seek be true if the auto-rewind flag is true and has finite timeline is false. Otherwise, initialize to false.
    auto enableSeek = autoRewind == AutoRewind::Yes && !hasFiniteTimeline;

    // 6. Perform the steps corresponding to the first matching condition from the following, if any::
    if ((playbackRate > 0 && enableSeek) && (!previousCurrentTime || *previousCurrentTime < 0_s || (*previousCurrentTime + previousCurrentTime->matchingEpsilon()) >= endTime)) {
        // If animation’s effective playback rate > 0, enable seek is true and either animation’s:
        //     - previous current time is unresolved, or
        //     - previous current time < zero, or
        //     - previous current time ≥ associated effect end,
        // Set the animation’s hold time to zero.
        m_holdTime = 0_s;
    } else if ((playbackRate < 0 && enableSeek) && (!previousCurrentTime || *previousCurrentTime <= 0_s || *previousCurrentTime > endTime)) {
        // If animation’s effective playback rate < 0, enable seek is true and either animation’s:
        //     - previous current time is unresolved, or
        //     - previous current time ≤ zero, or
        //     - previous current time > associated effect end,
        // If associated effect end is positive infinity, throw an "InvalidStateError" DOMException and abort these steps.
        //     throw an "InvalidStateError" DOMException and abort these steps.
        // Otherwise,
        //     Set the animation’s hold time to the animation’s associated effect end.
        if (endTime.isInfinity())
            return Exception { ExceptionCode::InvalidStateError };
        m_holdTime = endTime;
    } else if (!playbackRate && !previousCurrentTime) {
        // If animation’s effective playback rate = 0 and animation’s current time is unresolved,
        // Set the animation’s hold time to zero.
        m_holdTime = zeroTime();
    }

    // 7. If has finite timeline and previous current time is unresolved:
    // Set the flag auto align start time to true.
    if (hasFiniteTimeline && !previousCurrentTime)
        m_autoAlignStartTime = true;

    // 8. If animation’s hold time is resolved, let its start time be unresolved.
    if (m_holdTime)
        m_startTime = std::nullopt;

    // 9. If animation has a pending play task or a pending pause task,
    //     - Cancel that task.
    //     - Set has pending ready promise to true.
    if (pending()) {
        m_timeToRunPendingPauseTask = TimeToRunPendingTask::NotScheduled;
        m_timeToRunPendingPlayTask = TimeToRunPendingTask::NotScheduled;
        hasPendingReadyPromise = true;
    }

    // 10. If the following three conditions are all satisfied:
    //    - animation's hold time is unresolved, and
    //    - aborted pause is false, and
    //    - animation does not have a pending playback rate,
    // abort this procedure.
    // FIXME: the spec does not require the computation of pendingAutoAlignedStartTime
    // and accounting for it, but without it we never schedule a pending play task for
    // scroll-driven animations.
    auto pendingAutoAlignedStartTime = m_autoAlignStartTime && !m_startTime;
    if (!m_holdTime && !abortedPause && !m_pendingPlaybackRate && !pendingAutoAlignedStartTime)
        return { };

    // 11. If has pending ready promise is false, let animation's current ready promise be
    // a new promise in the relevant Realm of animation.
    if (!hasPendingReadyPromise)
        m_readyPromise = makeUniqueRef<ReadyPromise>(*this, &WebAnimation::readyPromiseResolve);

    // 12. Schedule a task to run as soon as animation is ready.
    m_timeToRunPendingPlayTask = TimeToRunPendingTask::WhenReady;

    // 13. Run the procedure to update an animation's finished state for animation with the did seek flag set to false, and the synchronously notify flag set to false.
    timingDidChange(DidSeek::No, SynchronouslyNotify::No);

    invalidateEffect();

    return { };
}

void WebAnimation::runPendingPlayTask()
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " runPendingPlayTask (current time is " << currentTime() << ")");

    // Playing an animation, step 12.
    // https://drafts.csswg.org/web-animations-2/#playing-an-animation-section

    m_timeToRunPendingPlayTask = TimeToRunPendingTask::NotScheduled;

    // 1. Assert that at least one of animation’s start time or hold time is resolved.
    ASSERT(m_startTime || m_holdTime);

    // 2. Let ready time be the time value of the timeline associated with animation at the moment when animation became ready.
    auto readyTime = m_pendingStartTime;
    if (!readyTime)
        readyTime = m_timeline->currentTime();

    // 3. Perform the steps corresponding to the first matching condition below, if any:
    if (m_holdTime) {
        // If animation's hold time is resolved,
        // 1. Apply any pending playback rate on animation.
        applyPendingPlaybackRate();
        // 2. Let new start time be the result of evaluating ready time - hold time / animation playback rate for animation.
        // If the animation playback rate is zero, let new start time be simply ready time.
        // FIXME: Implementation cannot guarantee an active timeline at the point of this async dispatch.
        // Subsequently, the resulting readyTime value can be null. Unify behavior between C++17 and
        // C++14 builds (the latter using WTF's Optional) and avoid null Optional dereferencing
        // by defaulting to a Seconds(0) value. See https://bugs.webkit.org/show_bug.cgi?id=186189.
        auto newStartTime = readyTime.value_or(0_s);
        if (m_playbackRate)
            newStartTime -= m_holdTime.value() / m_playbackRate;
        // 3. Set the start time of animation to new start time.
        m_startTime = newStartTime;
        // 4. If animation's playback rate is not 0, make animation's hold time unresolved.
        if (m_playbackRate)
            m_holdTime = std::nullopt;
    } else if (m_startTime && m_pendingPlaybackRate) {
        // If animation's start time is resolved and animation has a pending playback rate,
        // 1. Let current time to match be the result of evaluating (ready time - start time) × playback rate for animation.
        auto currentTimeToMatch = (readyTime.value_or(0_s) - m_startTime.value()) * m_playbackRate;
        // 2. Apply any pending playback rate on animation.
        applyPendingPlaybackRate();
        // 3. If animation's playback rate is zero, let animation's hold time be current time to match.
        if (m_playbackRate)
            m_holdTime = currentTimeToMatch;
        // 4. Let new start time be the result of evaluating ready time - current time to match / playback rate for animation.
        // If the playback rate is zero, let new start time be simply ready time.
        auto newStartTime = readyTime.value_or(0_s);
        if (m_playbackRate)
            newStartTime -= currentTimeToMatch / m_playbackRate;
        // 5. Set the start time of animation to new start time.
        m_startTime = newStartTime;
    }

    // 4. Resolve animation's current ready promise with animation.
    if (!m_readyPromise->isFulfilled())
        m_readyPromise->resolve(*this);

    // 5. Run the procedure to update an animation's finished state for animation with the did seek flag set to false, and the synchronously notify flag set to false.
    timingDidChange(DidSeek::No, SynchronouslyNotify::No, Silently::Yes);

    invalidateEffect();
}

ExceptionOr<void> WebAnimation::pause()
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " pause (current time is " << currentTime() << ")");

    // Pausing an animation
    // https://drafts.csswg.org/web-animations-2/#pausing-an-animation-section

    // 1. If animation has a pending pause task, abort these steps.
    if (hasPendingPauseTask())
        return { };

    // 2. If the play state of animation is paused, abort these steps.
    if (playState() == PlayState::Paused)
        return { };

    // 3. Let has finite timeline be true if animation has an associated timeline that is not monotonically increasing.
    auto hasFiniteTimeline = m_timeline && !m_timeline->isMonotonic();

    auto localTime = currentTime();
    // 4. If the animation’s current time is unresolved and has finite timeline is false, perform the steps according
    // to the first matching condition below:
    if (!localTime && !hasFiniteTimeline) {
        if (m_playbackRate >= 0) {
            // If animation's playback rate is ≥ 0, let animation's hold time be zero.
            m_holdTime = zeroTime();
        } else if (effectEndTime().isInfinity()) {
            // Otherwise, if target effect end for animation is positive infinity, throw an InvalidStateError and abort these steps.
            return Exception { ExceptionCode::InvalidStateError };
        } else {
            // Otherwise, let animation's hold time be target effect end.
            m_holdTime = effectEndTime();
        }
    }

    // 5. If has finite timeline is true, and the animation’s current time is unresolved,
    // Set the auto align start time flag to true.
    if (hasFiniteTimeline && !localTime)
        m_autoAlignStartTime = true;

    // 6. Let has pending ready promise be a boolean flag that is initially false.
    bool hasPendingReadyPromise = false;

    // 7. If animation has a pending play task, cancel that task and let has pending ready promise be true.
    if (hasPendingPlayTask()) {
        m_timeToRunPendingPlayTask = TimeToRunPendingTask::NotScheduled;
        hasPendingReadyPromise = true;
    }

    // 8. If has pending ready promise is false, set animation's current ready promise to a new (pending) Promise object.
    if (!hasPendingReadyPromise)
        m_readyPromise = makeUniqueRef<ReadyPromise>(*this, &WebAnimation::readyPromiseResolve);

    // 9. Schedule a task to be executed at the first possible moment where all of the following conditions are true:
    //     - the user agent has performed any processing necessary to suspend the playback of animation’s associated effect, if any.
    //     - the animation is associated with a timeline that is not inactive.
    //     - the animation has a resolved hold time or start time.
    m_timeToRunPendingPauseTask = TimeToRunPendingTask::ASAP;

    // 8. Run the procedure to update an animation's finished state for animation with the did seek flag set to false, and the synchronously notify flag set to false.
    timingDidChange(DidSeek::No, SynchronouslyNotify::No);

    invalidateEffect();

    return { };
}

ExceptionOr<void> WebAnimation::bindingsReverse()
{
    return reverse();
}

ExceptionOr<void> WebAnimation::reverse()
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " reverse (current time is " << currentTime() << ")");

    // 3.4.18. Reversing an animation
    // https://drafts.csswg.org/web-animations-1/#reverse-an-animation

    // The procedure to reverse an animation of animation animation is as follows:

    // 1. If there is no timeline associated with animation, or the associated timeline is inactive
    //    throw an InvalidStateError and abort these steps.
    if (!m_timeline || !m_timeline->currentTime())
        return Exception { ExceptionCode::InvalidStateError };

    // 2. Let original pending playback rate be animation's pending playback rate.
    auto originalPendingPlaybackRate = m_pendingPlaybackRate;

    // 3. Let animation's pending playback rate be the additive inverse of its effective playback rate (i.e. -effective playback rate).
    m_pendingPlaybackRate = -effectivePlaybackRate();

    // 4. Run the steps to play an animation for animation with the auto-rewind flag set to true.
    auto playResult = play(AutoRewind::Yes);

    // If the steps to play an animation throw an exception, set animation's pending playback rate to original
    // pending playback rate and propagate the exception.
    if (playResult.hasException()) {
        m_pendingPlaybackRate = originalPendingPlaybackRate;
        return playResult.releaseException();
    }

    if (m_effect)
        m_effect->animationDidChangeTimingProperties();

    return { };
}

void WebAnimation::runPendingPauseTask()
{
    LOG_WITH_STREAM(Animations, stream << "WebAnimation " << this << " runPendingPauseTask (current time is " << currentTime() << ")");

    // 3.4.11. Pausing an animation, step 10.
    // https://drafts.csswg.org/web-animations-1/#pause-an-animation

    m_timeToRunPendingPauseTask = TimeToRunPendingTask::NotScheduled;

    // 1. Let ready time be the time value of the timeline associated with animation at the moment when the user agent
    //    completed processing necessary to suspend playback of animation's target effect.
    auto readyTime = m_pendingStartTime;
    if (!readyTime)
        readyTime = m_timeline->currentTime();

    auto animationStartTime = m_startTime;

    // 2. If animation's start time is resolved and its hold time is not resolved, let animation's hold time be the result of
    //    evaluating (ready time - start time) × playback rate.
    //    Note: The hold time might be already set if the animation is finished, or if the animation is pending, waiting to begin
    //    playback. In either case we want to preserve the hold time as we enter the paused state.
    if (animationStartTime && !m_holdTime) {
        // FIXME: Implementation cannot guarantee an active timeline at the point of this async dispatch.
        // Subsequently, the resulting readyTime value can be null. Unify behavior between C++17 and
        // C++14 builds (the latter using WTF's Optional) and avoid null Optional dereferencing
        // by defaulting to a Seconds(0) value. See https://bugs.webkit.org/show_bug.cgi?id=186189.
        m_holdTime = (readyTime.value_or(0_s) - animationStartTime.value()) * m_playbackRate;
    }

    // 3. Apply any pending playback rate on animation.
    applyPendingPlaybackRate();

    // 4. Make animation's start time unresolved.
    m_startTime = std::nullopt;

    // 5. Resolve animation's current ready promise with animation.
    if (!m_readyPromise->isFulfilled())
        m_readyPromise->resolve(*this);

    // 6. Run the procedure to update an animation's finished state for animation with the did seek flag set to false, and the
    //    synchronously notify flag set to false.
    timingDidChange(DidSeek::No, SynchronouslyNotify::No, Silently::Yes);

    invalidateEffect();
}

void WebAnimation::autoAlignStartTime()
{
    // https://drafts.csswg.org/web-animations-2/#auto-aligning-start-time

    // When attached to a non-monotonic timeline, the start time of the animation may be layout dependent.
    // In this case, we defer calculation of the start time until the timeline has been updated post layout.
    // When updating timeline current time, the start time of any attached animation is conditionally updated.
    // The procedure for calculating an auto-aligned start time is as follows:

    // 1. If the auto-align start time flag is false, abort this procedure.
    if (!m_autoAlignStartTime)
        return;

    // 2. If the timeline is inactive, abort this procedure.
    if (!m_timeline || !m_timeline->currentTime())
        return;

    auto playState = this->playState();

    // 3. If play state is idle, abort this procedure.
    if (playState == PlayState::Idle)
        return;

    // 4. If play state is paused, and hold time is resolved, abort this procedure.
    if (playState == PlayState::Paused && m_holdTime)
        return;

    RefPtr scrollTimeline = dynamicDowncast<ScrollTimeline>(m_timeline);
    ASSERT(scrollTimeline);
    auto interval = scrollTimeline->intervalForAttachmentRange(range());

    // 5. Let start offset be the resolved timeline time corresponding to the start of the animation
    // attachment range. In the case of view timelines, it requires a calculation based on the proportion
    // of the cover range.
    auto startOffset = interval.first;

    // 6. Let end offset be the resolved timeline time corresponding to the end of the animation attachment
    // range. In the case of view timelines, it requires a calculation based on the proportion of the cover
    // range.
    auto endOffset = interval.second;

    // 7. Set start time to start offset if effective playback rate ≥ 0, and end offset otherwise.
    m_startTime = effectivePlaybackRate() >= 0 ? startOffset : endOffset;

    // 8. Clear hold time.
    m_holdTime = std::nullopt;

    progressBasedTimelineSourceDidChangeMetrics();
}

bool WebAnimation::needsTick() const
{
    return pending() || playState() == PlayState::Running || m_hasScheduledEventsDuringTick;
}

void WebAnimation::tick()
{
    // https://drafts.csswg.org/scroll-animations-1/#event-loop
    // When updating timeline current time, the start time of any attached animation is
    // conditionally updated. For each attached animation, run the procedure for calculating
    // an auto-aligned start time.
    if (m_timeline && m_timeline->isProgressBased())
        autoAlignStartTime();

    maybeMarkAsReady();

    m_hasScheduledEventsDuringTick = false;
    updateFinishedState(DidSeek::No, SynchronouslyNotify::Yes);
    m_shouldSkipUpdatingFinishedStateWhenResolving = true;

    if (!isEffectInvalidationSuspended() && m_effect)
        m_effect->animationDidTick();
}

void WebAnimation::maybeMarkAsReady()
{
    // https://drafts.csswg.org/web-animations-2/#ready
    // An animation is ready at the first moment where all of the following conditions are true:
    //     - the user agent has completed any setup required to begin the playback of each inclusive
    //       descendant of the animation’s associated effect including rendering the first frame of
    //       any keyframe effect or executing any custom effects associated with an animation effect.
    //     - the animation is associated with a timeline that is not inactive.
    //     - the animation’s hold time or start time is resolved.
    if (!pending())
        return;

    auto isReady = m_timeline && m_timeline->currentTime() && (m_holdTime || m_startTime);
    if (!isReady)
        return;

    // Monotonic animations also require a pending start time.
    if (!m_pendingStartTime && m_timeline->isMonotonic())
        return;

    // The effect can also prevent readines.
    if (m_effect && m_effect->preventsAnimationReadiness())
        return;

    if (hasPendingPauseTask())
        runPendingPauseTask();
    if (hasPendingPlayTask())
        runPendingPlayTask();

    m_pendingStartTime = std::nullopt;
}

OptionSet<AnimationImpact> WebAnimation::resolve(RenderStyle& targetStyle, const Style::ResolutionContext& resolutionContext)
{
    if (!m_shouldSkipUpdatingFinishedStateWhenResolving)
        updateFinishedState(DidSeek::No, SynchronouslyNotify::No);
    m_shouldSkipUpdatingFinishedStateWhenResolving = false;

    if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect))
        return keyframeEffect->apply(targetStyle, resolutionContext);
    return { };
}

void WebAnimation::setSuspended(bool isSuspended)
{
    if (m_isSuspended == isSuspended)
        return;

    m_isSuspended = isSuspended;

    if (m_effect && playState() == PlayState::Running)
        m_effect->animationSuspensionStateDidChange(isSuspended);
}

void WebAnimation::acceleratedStateDidChange()
{
    if (RefPtr documentTimeline = dynamicDowncast<DocumentTimeline>(m_timeline))
        documentTimeline->animationAcceleratedRunningStateDidChange(*this);
}

WebAnimation& WebAnimation::readyPromiseResolve()
{
    return *this;
}

WebAnimation& WebAnimation::finishedPromiseResolve()
{
    return *this;
}

void WebAnimation::suspend(ReasonForSuspension)
{
    setSuspended(true);
}

void WebAnimation::resume()
{
    setSuspended(false);
}

void WebAnimation::stop()
{
    ActiveDOMObject::stop();
    removeAllEventListeners();
}

bool WebAnimation::virtualHasPendingActivity() const
{
    // Keep the JS wrapper alive if the animation is considered relevant or could become relevant again by virtue of having a timeline.
    return m_timeline || m_isRelevant;
}

void WebAnimation::updateRelevance()
{
    auto wasRelevant = std::exchange(m_isRelevant, computeRelevance());
    if (wasRelevant != m_isRelevant) {
        if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect))
            keyframeEffect->animationRelevancyDidChange();
    }
}

bool WebAnimation::computeRelevance()
{
    // https://drafts.csswg.org/web-animations-1/#relevant-animations-section
    // https://drafts.csswg.org/web-animations-1/#current
    // https://drafts.csswg.org/web-animations-1/#in-effect

    // An animation is relevant if:
    // - its associated effect is current or in effect, and
    if (!m_effect)
        return false;

    // - its replace state is not removed.
    if (m_replaceState == ReplaceState::Removed)
        return false;

    auto timing = m_effect->getBasicTiming();

    // An animation effect is in play if all of the following conditions are met:
    // - the animation effect is in the active phase, and
    // - the animation effect is associated with an animation that is not finished.
    if (timing.phase == AnimationEffectPhase::Active && playState() != PlayState::Finished)
        return true;

    // An animation effect is current if any of the following conditions are true:
    // - the animation effect is in play, or
    // - the animation effect is associated with an animation with a playback rate > 0 and the animation effect is in the before phase, or
    if (m_playbackRate > 0 && timing.phase == AnimationEffectPhase::Before)
        return true;

    // - the animation effect is associated with an animation with a playback rate < 0 and the animation effect is in the after phase.
    if (m_playbackRate < 0 && timing.phase == AnimationEffectPhase::After)
        return true;

    // - the animation effect is associated with an animation not in the idle play state with a non-null
    // associated timeline that is not monotonically increasing.
    if (m_timeline && !m_timeline->isMonotonic() && playState() != PlayState::Idle)
        return true;

    // An animation effect is in effect if its active time, as calculated according to the procedure in
    // § 4.8.3.1 Calculating the active time, is not unresolved.
    if (timing.activeTime)
        return true;

    return false;
}

bool WebAnimation::isReplaceable() const
{
    // An animation is replaceable if all of the following conditions are true:
    // https://drafts.csswg.org/web-animations/#removing-replaced-animations

    // The existence of the animation is not prescribed by markup. That is, it is not a CSS animation with an owning element,
    // nor a CSS transition with an owning element.
    auto* styleOriginatedAnimation = dynamicDowncast<StyleOriginatedAnimation>(*this);
    if (styleOriginatedAnimation && styleOriginatedAnimation->owningElement())
        return false;

    // The animation's play state is finished.
    if (playState() != PlayState::Finished)
        return false;

    // The animation's replace state is not removed.
    if (m_replaceState == ReplaceState::Removed)
        return false;

    // The animation is associated with a monotonically increasing timeline.
    if (!m_timeline)
        return false;

    // The animation has an associated target effect.
    if (!m_effect)
        return false;

    // The target effect associated with the animation is in effect.
    if (!m_effect->getBasicTiming().activeTime)
        return false;

    // The target effect has an associated target element.
    RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect);
    if (!keyframeEffect || !keyframeEffect->target())
        return false;

    return true;
}

void WebAnimation::persist()
{
    setReplaceState(ReplaceState::Persisted);
}

void WebAnimation::setReplaceState(ReplaceState replaceState)
{
    if (m_replaceState == replaceState)
        return;

    m_replaceState = replaceState;
    updateRelevance();
}

ExceptionOr<void> WebAnimation::commitStyles()
{
    // https://drafts.csswg.org/web-animations-1/#commit-computed-styles

    // 1. Let targets be the set of all effect targets for animation effects associated with animation.
    RefPtr effect = dynamicDowncast<KeyframeEffect>(m_effect);

    // 2. For each target in targets:
    //
    // 2.1 If target is not an element capable of having a style attribute (for example, it is a pseudo-element or is an element in a
    // document format for which style attributes are not defined) throw a "NoModificationAllowedError" DOMException and abort these steps.
    RefPtr styledElement = dynamicDowncast<StyledElement>(effect ? effect->target() : nullptr);
    if (!styledElement || effect->targetsPseudoElement())
        return Exception { ExceptionCode::NoModificationAllowedError };

    // 2.2 If, after applying any pending style changes, target is not being rendered, throw an "InvalidStateError" DOMException and abort these steps.
    styledElement->protectedDocument()->updateStyleIfNeeded();
    auto* renderer = styledElement->renderer();
    if (!renderer)
        return Exception { ExceptionCode::InvalidStateError };

    // 2.3 Let inline style be the result of getting the CSS declaration block corresponding to target’s style attribute. If target does not have a style
    // attribute, let inline style be a new empty CSS declaration block with the readonly flag unset and owner node set to target.

    auto unanimatedStyle = [&]() {
        if (auto styleable = Styleable::fromRenderer(*renderer)) {
            if (auto* lastStyleChangeEventStyle = styleable->lastStyleChangeEventStyle())
                return RenderStyle::clone(*lastStyleChangeEventStyle);
        }
        // If we don't have a style for the last style change event, then the
        // current renderer style cannot be animated.
        return RenderStyle::clone(renderer->style());
    }();

    Style::Extractor computedStyleExtractor { styledElement.get() };

    auto inlineStyle = [&]() {
        if (RefPtr existinInlineStyle = styledElement->inlineStyle())
            return existinInlineStyle->mutableCopy();
        auto styleDeclaration = styledElement->document().createCSSStyleDeclaration();
        styleDeclaration->setCssText(styledElement->getAttribute(HTMLNames::styleAttr));
        return styleDeclaration->copyProperties();
    }();

    auto& keyframeStack = styledElement->ensureKeyframeEffectStack({ });

    auto commitProperty = [&](AnimatableCSSProperty property) {
        // 1. Let partialEffectStack be a copy of the effect stack for property on target.
        // 2. If animation's replace state is removed, add all animation effects associated with animation whose effect target is target and which include
        // property as a target property to partialEffectStack.
        // 3. Remove from partialEffectStack any animation effects whose associated animation has a higher composite order than animation.
        // 4. Let effect value be the result of calculating the result of partialEffectStack for property using target's computed style (see § 5.4.3 Calculating
        // the result of an effect stack).
        // 5. Set a CSS declaration property for effect value in inline style.
        // 6. Update style attribute for inline style.

        // We actually perform those steps in a different way: instead of building a copy of the effect stack and then removing stuff, we iterate through the
        // effect stack and stop when we've found this animation's effect or when we've found an effect associated with an animation with a higher composite order.
        auto animatedStyle = RenderStyle::clonePtr(unanimatedStyle);
        for (const auto& effectInStack : keyframeStack.sortedEffects()) {
            if (effectInStack->animation() != this && !compareAnimationsByCompositeOrder(*effectInStack->animation(), *this))
                break;
            if (effectInStack->animatedProperties().contains(property))
                effectInStack->animation()->resolve(*animatedStyle, { nullptr });
            if (effectInStack->animation() == this)
                break;
        }
        if (m_replaceState == ReplaceState::Removed)
            effect->animation()->resolve(*animatedStyle, { nullptr });
        return WTF::switchOn(property,
            [&](CSSPropertyID propertyId) {
                auto string = computedStyleExtractor.propertyValueSerializationInStyle(*animatedStyle, propertyId, CSS::defaultSerializationContext(), CSSValuePool::singleton(), nullptr, Style::ExtractorState::PropertyValueType::Computed);
                if (!string.isEmpty())
                    return inlineStyle->setProperty(propertyId, WTFMove(string), { styledElement->document() });
                return false;
            },
            [&](const AtomString& customProperty) {
                auto string = computedStyleExtractor.customPropertyValueSerialization(customProperty, CSS::defaultSerializationContext());
                if (!string.isEmpty())
                    return inlineStyle->setCustomProperty(customProperty, WTFMove(string), { styledElement->document() });
                return false;
            }
        );
    };

    // 2.4 Let targeted properties be the set of physical longhand properties that are a target property for at least one
    // animation effect associated with animation whose effect target is target.
    HashSet<AnimatableCSSProperty> targetedProperties;
    for (auto property : effect->animatedProperties()) {
        if (std::holds_alternative<CSSPropertyID>(property)) {
            for (auto longhand : shorthandForProperty(std::get<CSSPropertyID>(property)))
                targetedProperties.add(longhand);
        }
        targetedProperties.add(property);
    }
    // 2.5 For each property, property, in targeted properties:
    auto didMutate = false;
    for (auto property : targetedProperties)
        didMutate = commitProperty(property) || didMutate;

    if (didMutate)
        styledElement->setAttribute(HTMLNames::styleAttr, inlineStyle->asTextAtom(CSS::defaultSerializationContext()));

    return { };
}

Seconds WebAnimation::timeToNextTick() const
{
    if (pending())
        return 0_s;

    // If we're not running, or time is not advancing for this animation, there's no telling when we'll end.
    auto playbackRate = effectivePlaybackRate();
    if (playState() != PlayState::Running || !playbackRate)
        return Seconds::infinity();

    ASSERT(effect());
    return effect()->timeToNextTick(effect()->getBasicTiming()) / playbackRate;
}

std::optional<Seconds> WebAnimation::convertAnimationTimeToTimelineTime(Seconds animationTime) const
{
    // https://drafts.csswg.org/web-animations-1/#animation-time-to-timeline-time
    // To convert an animation time to timeline time a time value, time, that is relative to the start time
    // of an animation, animation, perform the following steps:
    //
    // 1. If time is unresolved, return time.
    // 2. If time is infinity, return an unresolved time value.
    // 3. If animation's playback rate is zero, return an unresolved time value.
    // 4. If animation's start time is unresolved, return an unresolved time value.
    if (!m_playbackRate || !m_startTime || animationTime.isInfinity())
        return std::nullopt;
    // 5. Return the result of calculating: time × (1 / playback rate) + start time (where playback rate and start time are the playback rate and start time of animation, respectively).
    return animationTime * (1 / m_playbackRate) + *m_startTime;
}

bool WebAnimation::isSkippedContentAnimation() const
{
    if (pending())
        return false;
    if (auto animation = dynamicDowncast<StyleOriginatedAnimation>(this)) {
        if (auto element = animation->owningElement())
            return element->element.renderer() && element->element.renderer()->isSkippedContent();
    }
    return false;
}

std::optional<double> WebAnimation::overallProgress() const
{
    // https://drafts.csswg.org/web-animations-2/#the-overall-progress-of-an-animation
    // An animation's overallProgress is the ratio of its current time to its associated effect end.
    //
    // The overallProgress of an animation, animation, is calculated as follows:
    //
    // If any of the following are true:
    //     - animation does not have an associated effect, or
    //     - animation's current time is an unresolved time value,
    // animation's overallProgress is null.
    if (!m_effect)
        return std::nullopt;

    auto currentTime = this->currentTime();
    if (!currentTime)
        return std::nullopt;

    auto endTime = effectEndTime();

    // If animation's associated effect end is zero,
    //     - If animation's current time is negative, animation's overallProgress is zero.
    //     - Otherwise, animation's overallProgress is one.
    if (endTime.isZero())
        return *currentTime < endTime.matchingZero() ? 0 : 1;

    // If animation's associated effect end is infinite, animation's overallProgress is zero.
    if (endTime.isInfinity())
        return 0;

    // Otherwise, overallProgress = min(max(current time / animation's associated effect end, 0), 1)
    return std::min(std::max(*currentTime / endTime, 0.0), 1.0);
}

void WebAnimation::setBindingsRangeStart(TimelineRangeValue&& rangeStartValue)
{
    RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect);
    if (!keyframeEffect)
        return;

    auto rangeStart = SingleTimelineRange::parse(WTFMove(rangeStartValue), keyframeEffect->target(), SingleTimelineRange::Type::Start);
    if (m_specifiedRangeStart == rangeStart)
        return;

    m_specifiedRangeStart = WTFMove(rangeStart);
    if (RefPtr effect = this->effect())
        effect->animationRangeDidChange();
}

void WebAnimation::setBindingsRangeEnd(TimelineRangeValue&& rangeEndValue)
{
    RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect);
    if (!keyframeEffect)
        return;

    auto rangeEnd = SingleTimelineRange::parse(WTFMove(rangeEndValue), keyframeEffect->target(), SingleTimelineRange::Type::End);
    if (m_specifiedRangeEnd == rangeEnd)
        return;

    m_specifiedRangeEnd = WTFMove(rangeEnd);
    if (RefPtr effect = this->effect())
        effect->animationRangeDidChange();
}

void WebAnimation::setRangeStart(SingleTimelineRange rangeStart)
{
    if (m_timelineRange.start == rangeStart)
        return;

    m_timelineRange.start = rangeStart;
    if (RefPtr effect = this->effect())
        effect->animationRangeDidChange();
}

void WebAnimation::setRangeEnd(SingleTimelineRange rangeEnd)
{
    if (m_timelineRange.end == rangeEnd)
        return;

    m_timelineRange.end = rangeEnd;
    if (RefPtr effect = this->effect())
        effect->animationRangeDidChange();
}

const TimelineRange& WebAnimation::range()
{
    if (RefPtr keyframeEffect = dynamicDowncast<KeyframeEffect>(m_effect)) {
        if (m_specifiedRangeStart)
            m_timelineRange.start = SingleTimelineRange::range(*m_specifiedRangeStart, SingleTimelineRange::Type::Start, nullptr, keyframeEffect->target());
        if (m_specifiedRangeEnd)
            m_timelineRange.end = SingleTimelineRange::range(*m_specifiedRangeEnd, SingleTimelineRange::Type::End, nullptr, keyframeEffect->target());
    }

    return m_timelineRange;
}

void WebAnimation::progressBasedTimelineSourceDidChangeMetrics()
{
    ASSERT(m_timeline && m_timeline->isProgressBased());
    if (RefPtr effect = m_effect)
        effect->animationProgressBasedTimelineSourceDidChangeMetrics(range());
}

} // namespace WebCore
