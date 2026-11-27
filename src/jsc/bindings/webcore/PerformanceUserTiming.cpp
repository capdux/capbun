/*
 * Copyright (C) 2012 Intel Inc. All rights reserved.
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
#include "PerformanceUserTiming.h"

// #include "Document.h"
// #include "FrameDestructionObserverInlines.h"
// #include "InspectorInstrumetation.h"
#include "MessagePort.h"
#include "PerformanceMarkOptions.h"
#include "PerformanceMeasureOptions.h"
#include "PerformanceTiming.h"
#include "SerializedScriptValue.h"
// #include "WorkerOrWorkletGlobalScope.h"
#include <JavaScriptCore/JSCJSValueInlines.h>
#include <wtf/SortedArrayMap.h>

namespace WebCore {

using NavigationTimingFunction = unsigned long long (PerformanceTiming::*)() const;

static constexpr SortedArrayMap restrictedMarkFunctions { std::to_array<std::pair<ComparableASCIILiteral, NavigationTimingFunction>>({
    { "connectEnd"_s, &PerformanceTiming::connectEnd },
    { "connectStart"_s, &PerformanceTiming::connectStart },
    { "domComplete"_s, &PerformanceTiming::domComplete },
    { "domContentLoadedEventEnd"_s, &PerformanceTiming::domContentLoadedEventEnd },
    { "domContentLoadedEventStart"_s, &PerformanceTiming::domContentLoadedEventStart },
    { "domInteractive"_s, &PerformanceTiming::domInteractive },
    { "domLoading"_s, &PerformanceTiming::domLoading },
    { "domainLookupEnd"_s, &PerformanceTiming::domainLookupEnd },
    { "domainLookupStart"_s, &PerformanceTiming::domainLookupStart },
    { "fetchStart"_s, &PerformanceTiming::fetchStart },
    { "loadEventEnd"_s, &PerformanceTiming::loadEventEnd },
    { "loadEventStart"_s, &PerformanceTiming::loadEventStart },
    { "navigationStart"_s, &PerformanceTiming::navigationStart },
    { "redirectEnd"_s, &PerformanceTiming::redirectEnd },
    { "redirectStart"_s, &PerformanceTiming::redirectStart },
    { "requestStart"_s, &PerformanceTiming::requestStart },
    { "responseEnd"_s, &PerformanceTiming::responseEnd },
    { "responseStart"_s, &PerformanceTiming::responseStart },
    { "secureConnectionStart"_s, &PerformanceTiming::secureConnectionStart },
    { "unloadEventEnd"_s, &PerformanceTiming::unloadEventEnd },
    { "unloadEventStart"_s, &PerformanceTiming::unloadEventStart },
}) };

bool PerformanceUserTiming::isRestrictedMarkName(const String& markName)
{
    return restrictedMarkFunctions.contains(markName);
}

PerformanceUserTiming::PerformanceUserTiming(Performance& performance)
    : m_performance(performance)
{
}

size_t PerformanceUserTiming::memoryCost() const
{
    size_t size = sizeof(PerformanceUserTiming);
    size += m_marksMap.byteSize();
    size += m_measuresMap.byteSize();

    for (const auto& entry : m_marksMap) {
        size += entry.value.sizeInBytes();
        for (const auto& entry : entry.value) {
            size += entry->memoryCost();
        }
    }

    for (const auto& entry : m_measuresMap) {
        size += entry.value.sizeInBytes();
        for (const auto& entry : entry.value) {
            size += entry->memoryCost();
        }
    }

    return size;
}

static size_t clearPerformanceEntries(PerformanceEntryMap& map, const String& name)
{
    if (name.isNull()) {
        map.clear();
        return std::numeric_limits<size_t>::max();
    }
    auto it = map.find(name);
    if (it == map.end())
        return 0;
    size_t removed = it->value.size();
    map.remove(it);
    return removed;
}

static void addPerformanceEntry(PerformanceEntryMap& map, const String& name, PerformanceEntry& entry)
{
    auto& performanceEntryList = map.ensure(name, [] { return Vector<RefPtr<PerformanceEntry>>(); }).iterator->value;
    performanceEntryList.append(&entry);
}

ExceptionOr<Ref<PerformanceMark>> PerformanceUserTiming::mark(JSC::JSGlobalObject& globalObject, const String& markName, std::optional<PerformanceMarkOptions>&& markOptions)
{
    auto& context = *m_performance.scriptExecutionContext();

    std::optional<MonotonicTime> timestamp;
    if (markOptions && markOptions->startTime)
        timestamp = m_performance.monotonicTimeFromRelativeTime(*markOptions->startTime);

    // InspectorInstrumentation::performanceMark(context.get(), markName, timestamp, nullptr);

    auto mark = PerformanceMark::create(globalObject, context, markName, WTF::move(markOptions));
    if (mark.hasException())
        return mark.releaseException();

    addPerformanceEntry(m_marksMap, markName, mark.returnValue().get());
    m_markCounter += 1;
    return mark.releaseReturnValue();
}

void PerformanceUserTiming::clearMarks(const String& markName)
{
    size_t removed = clearPerformanceEntries(m_marksMap, markName);
    if (markName.isNull())
        m_markCounter = 0;
    else
        m_markCounter -= std::min(m_markCounter, static_cast<int64_t>(removed));
}

ExceptionOr<double> PerformanceUserTiming::convertMarkToTimestamp(const std::variant<String, double>& mark) const
{
    return std::visit([&](auto& value) {
        return convertMarkToTimestamp(value);
    },
        mark);
}

ExceptionOr<double> PerformanceUserTiming::convertMarkToTimestamp(const String& mark) const
{
    // if (!isMainThread()) {
    //     if (restrictedMarkFunctions.contains(mark))
    //         return Exception { TypeError };
    // } else {
    //     if (auto function = restrictedMarkFunctions.tryGet(mark)) {
    //         if (*function == &PerformanceTiming::navigationStart)
    //             return 0.0;

    //         // PerformanceTiming should always be non-null for the Document ScriptExecutionContext.
    //         ASSERT(m_performance.timing());
    //         auto timing = m_performance.timing();
    //         auto startTime = timing->navigationStart();
    //         auto endTime = ((*timing).*(*function))();
    //         if (!endTime)
    //             return Exception { InvalidAccessError };
    //         return endTime - startTime;
    //     }
    // }

    auto iterator = m_marksMap.find(mark);
    if (iterator != m_marksMap.end())
        return iterator->value.last()->startTime();

    auto message = tryMakeString("No mark named '"_s, mark, "' exists"_s);
    if (message.isNull()) [[unlikely]]
        return Exception { RangeError, "Out of memory"_s };
    return Exception { SyntaxError, WTF::move(message) };
}

ExceptionOr<double> PerformanceUserTiming::convertMarkToTimestamp(double mark) const
{
    if (mark < 0)
        return Exception { TypeError };
    return mark;
}

ExceptionOr<Ref<PerformanceMeasure>> PerformanceUserTiming::measure(const String& measureName, const String& startMark, const String& endMark)
{
    double endTime;
    if (!endMark.isNull()) {
        auto end = convertMarkToTimestamp(endMark);
        if (end.hasException())
            return end.releaseException();
        endTime = end.returnValue();
    } else
        endTime = m_performance.now();

    double startTime;
    if (!startMark.isNull()) {
        auto start = convertMarkToTimestamp(startMark);
        if (start.hasException())
            return start.releaseException();
        startTime = start.returnValue();
    } else
        startTime = 0.0;

    auto measure = PerformanceMeasure::create(measureName, startTime, endTime, nullptr);
    if (measure.hasException())
        return measure.releaseException();

    addPerformanceEntry(m_measuresMap, measureName, measure.returnValue().get());
    m_measureCounter += 1;
    return measure.releaseReturnValue();
}

ExceptionOr<Ref<PerformanceMeasure>> PerformanceUserTiming::measure(JSC::JSGlobalObject& globalObject, const String& measureName, const PerformanceMeasureOptions& measureOptions)
{
    double endTime;
    if (measureOptions.end) {
        auto end = convertMarkToTimestamp(*measureOptions.end);
        if (end.hasException())
            return end.releaseException();
        endTime = end.returnValue();
    } else if (measureOptions.start && measureOptions.duration) {
        auto start = convertMarkToTimestamp(*measureOptions.start);
        if (start.hasException())
            return start.releaseException();
        auto duration = convertMarkToTimestamp(*measureOptions.duration);
        if (duration.hasException())
            return duration.releaseException();
        endTime = start.returnValue() + duration.returnValue();
    } else
        endTime = m_performance.now();

    double startTime;
    if (measureOptions.start) {
        auto start = convertMarkToTimestamp(*measureOptions.start);
        if (start.hasException())
            return start.releaseException();
        startTime = start.returnValue();
    } else if (measureOptions.duration && measureOptions.end) {
        auto duration = convertMarkToTimestamp(*measureOptions.duration);
        if (duration.hasException())
            return duration.releaseException();
        auto end = convertMarkToTimestamp(*measureOptions.end);
        if (end.hasException())
            return end.releaseException();
        startTime = end.returnValue() - duration.returnValue();
    } else
        startTime = 0;

    JSC::JSValue detail = measureOptions.detail;
    if (detail.isUndefined())
        detail = JSC::jsNull();

    if (!detail.isNull()) {
        Vector<RefPtr<MessagePort>> ignoredMessagePorts;
        auto serializedDetail = SerializedScriptValue::create(globalObject, detail, {}, ignoredMessagePorts);
        if (serializedDetail.hasException())
            return serializedDetail.releaseException();

        auto measure = PerformanceMeasure::create(measureName, startTime, endTime, serializedDetail.releaseReturnValue());
        if (measure.hasException())
            return measure.releaseException();

        addPerformanceEntry(m_measuresMap, measureName, measure.returnValue().get());
        m_measureCounter += 1;
        return measure.releaseReturnValue();
    } else {
        auto measure = PerformanceMeasure::create(measureName, startTime, endTime, nullptr);
        if (measure.hasException())
            return measure.releaseException();

        addPerformanceEntry(m_measuresMap, measureName, measure.returnValue().get());
        m_measureCounter += 1;
        return measure.releaseReturnValue();
    }
}

ExceptionOr<Ref<PerformanceMeasure>> PerformanceUserTiming::measure(JSC::JSGlobalObject& globalObject, const String& measureName, std::optional<StartOrMeasureOptions>&& startOrMeasureOptions, const String& endMark)
{
    if (startOrMeasureOptions) {
        return std::visit(
            WTF::makeVisitor(
                [&](const PerformanceMeasureOptions& measureOptions) -> ExceptionOr<Ref<PerformanceMeasure>> {
                    if (measureOptions.start || measureOptions.end) {
                        if (!endMark.isNull())
                            return Exception { TypeError };
                        if (measureOptions.start && measureOptions.duration && measureOptions.end)
                            return Exception { TypeError };
                        return measure(globalObject, measureName, measureOptions);
                    }

                    if (!endMark.isNull()) {
                        auto end = convertMarkToTimestamp(endMark);
                        if (end.hasException())
                            return end.releaseException();
                        double endTime = end.returnValue();
                        double startTime = 0.0;

                        JSC::JSValue detail = measureOptions.detail;
                        if (detail.isUndefined())
                            detail = JSC::jsNull();

                        if (!detail.isNull()) {
                            Vector<RefPtr<MessagePort>> ignoredMessagePorts;
                            auto serializedDetail = SerializedScriptValue::create(globalObject, detail, {}, ignoredMessagePorts);
                            if (serializedDetail.hasException())
                                return serializedDetail.releaseException();

                            auto m = PerformanceMeasure::create(measureName, startTime, endTime, serializedDetail.releaseReturnValue());
                            if (m.hasException())
                                return m.releaseException();

                            addPerformanceEntry(m_measuresMap, measureName, m.returnValue().get());
                            m_measureCounter += 1;
                            return m.releaseReturnValue();
                        } else {
                            auto m = PerformanceMeasure::create(measureName, startTime, endTime, nullptr);
                            if (m.hasException())
                                return m.releaseException();

                            addPerformanceEntry(m_measuresMap, measureName, m.returnValue().get());
                            m_measureCounter += 1;
                            return m.releaseReturnValue();
                        }
                    }

                    if (measureOptions.duration)
                        return Exception { TypeError };

                    return measure(globalObject, measureName, measureOptions);
                },
                [&](const String& startMark) -> ExceptionOr<Ref<PerformanceMeasure>> {
                    return measure(measureName, startMark, endMark);
                }),
            *startOrMeasureOptions);
    }

    return measure(measureName, {}, endMark);
}

void PerformanceUserTiming::clearMeasures(const String& measureName)
{
    size_t removed = clearPerformanceEntries(m_measuresMap, measureName);
    if (measureName.isNull())
        m_measureCounter = 0;
    else
        m_measureCounter -= std::min(m_measureCounter, static_cast<int64_t>(removed));
}

static Vector<RefPtr<PerformanceEntry>> convertToEntrySequence(const PerformanceEntryMap& map, int64_t initialCapacity)
{
    Vector<RefPtr<PerformanceEntry>> entries;
    size_t safeInitialCapacity = initialCapacity > 0 && initialCapacity <= std::numeric_limits<int32_t>::max() ? static_cast<size_t>(initialCapacity) : 0;
    if (safeInitialCapacity)
        entries.reserveInitialCapacity(safeInitialCapacity);

    for (auto& entry : map.values())
        entries.appendVector(entry);

    ASSERT(entries.size() <= safeInitialCapacity);
    return entries;
}

Vector<RefPtr<PerformanceEntry>> PerformanceUserTiming::getMarks() const
{
    return convertToEntrySequence(m_marksMap, m_markCounter);
}

Vector<RefPtr<PerformanceEntry>> PerformanceUserTiming::getMarks(const String& name) const
{
    return m_marksMap.get(name);
}

Vector<RefPtr<PerformanceEntry>> PerformanceUserTiming::getMeasures() const
{
    return convertToEntrySequence(m_measuresMap, m_measureCounter);
}

Vector<RefPtr<PerformanceEntry>> PerformanceUserTiming::getMeasures(const String& name) const
{
    return m_measuresMap.get(name);
}

} // namespace WebCore
