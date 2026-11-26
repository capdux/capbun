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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "URLSearchParams.h"

#include "DOMURL.h"
#include <wtf/URLParser.h>
#include "helpers.h"
#include "JSURLSearchParams.h"
#include "JSDOMExceptionHandling.h"
#include <wtf/ASCIICType.h>

extern "C" size_t Bun__stringSyntheticAllocationLimit;

namespace WebCore {

static ExceptionOr<String> tryFormURLDecodeSegment(StringView segment)
{
    if (segment.isEmpty())
        return String { emptyString() };

    if (segment.containsOnlyASCII() && !segment.contains('+') && !segment.contains('%')) {
        if (segment.length() > Bun__stringSyntheticAllocationLimit) [[unlikely]]
            return Exception { OutOfMemoryError };
        return segment.toString();
    }

    const size_t maxDecodedBytes = Bun__stringSyntheticAllocationLimit / 2;
    size_t decodedUTF8Len = 0;
    if (segment.is8Bit()) {
        auto span = segment.span8();
        for (size_t i = 0; i < span.size(); ++i) {
            auto c = span[i];
            if (c == '%' && i + 2 < span.size() && isASCIIHexDigit(span[i + 1]) && isASCIIHexDigit(span[i + 2])) {
                decodedUTF8Len += 1;
                i += 2;
            } else if (c < 0x80) {
                decodedUTF8Len += 1;
            } else {
                decodedUTF8Len += 2;
            }
            if (decodedUTF8Len > maxDecodedBytes) [[unlikely]]
                return Exception { OutOfMemoryError };
        }
    } else {
        auto span = segment.span16();
        for (size_t i = 0; i < span.size(); ++i) {
            char16_t c = span[i];
            if (c == '%' && i + 2 < span.size() && isASCIIHexDigit(span[i + 1]) && isASCIIHexDigit(span[i + 2])) {
                decodedUTF8Len += 1;
                i += 2;
            } else if (c < 0x80) {
                decodedUTF8Len += 1;
            } else if (c < 0x800) {
                decodedUTF8Len += 2;
            } else if (U16_IS_LEAD(c) && i + 1 < span.size() && U16_IS_TRAIL(span[i + 1])) {
                decodedUTF8Len += 4;
                i += 1;
            } else {
                decodedUTF8Len += 3;
            }
            if (decodedUTF8Len > maxDecodedBytes) [[unlikely]]
                return Exception { OutOfMemoryError };
        }
    }

    std::optional<String> decoded;
    if (segment.contains('+')) {
        if (segment.is8Bit()) {
            Vector<Latin1Character> buf(segment.span8());
            for (auto& c : buf) {
                if (c == '+')
                    c = ' ';
            }
            decoded = WTF::URLParser::formURLDecode(StringView(buf.span()));
        } else {
            Vector<char16_t> buf(segment.span16());
            for (auto& c : buf) {
                if (c == '+')
                    c = ' ';
            }
            decoded = WTF::URLParser::formURLDecode(StringView(buf.span()));
        }
    } else {
        decoded = WTF::URLParser::formURLDecode(segment);
    }
    if (!decoded || decoded->length() > Bun__stringSyntheticAllocationLimit) [[unlikely]]
        return Exception { OutOfMemoryError };
    return WTF::move(*decoded);
}

ExceptionOr<Vector<KeyValuePair<String, String>>> URLSearchParams::tryParseURLEncodedForm(StringView input)
{
    Vector<KeyValuePair<String, String>> result;
    const size_t maxPairs = Bun__stringSyntheticAllocationLimit / sizeof(KeyValuePair<String, String>);

    size_t start = 0;
    while (start <= input.length()) {
        auto ampIndex = input.find('&', start);
        size_t end = ampIndex == WTF::notFound ? input.length() : ampIndex;
        if (end > start) {
            auto segment = input.substring(start, end - start);
            auto equalIndex = segment.find('=');
            StringView rawName = equalIndex == WTF::notFound ? segment : segment.left(equalIndex);
            StringView rawValue = equalIndex == WTF::notFound ? StringView() : segment.substring(equalIndex + 1);

            auto name = tryFormURLDecodeSegment(rawName);
            if (name.hasException())
                return name.releaseException();
            auto value = tryFormURLDecodeSegment(rawValue);
            if (value.hasException())
                return value.releaseException();

            if (result.size() >= maxPairs) [[unlikely]]
                return Exception { OutOfMemoryError };
            if (!result.tryAppend({ name.releaseReturnValue(), value.releaseReturnValue() })) [[unlikely]]
                return Exception { OutOfMemoryError };
        }
        if (ampIndex == WTF::notFound)
            break;
        start = ampIndex + 1;
    }

    return result;
}

extern "C" JSC::EncodedJSValue URLSearchParams__create(JSDOMGlobalObject* globalObject, const ZigString* input)
{
    auto scope = DECLARE_THROW_SCOPE(globalObject->vm());
    String str = Zig::toString(*input);
    auto result = URLSearchParams::create(str, nullptr);
    if (result.hasException()) [[unlikely]] {
        propagateException(*globalObject, scope, result.releaseException());
        return JSC::JSValue::encode(JSC::JSValue());
    }
    return JSC::JSValue::encode(WebCore::toJSNewlyCreated(globalObject, globalObject, result.releaseReturnValue()));
}

extern "C" WebCore::URLSearchParams* URLSearchParams__fromJS(JSC::EncodedJSValue value)
{
    return WebCoreCast<WebCore::JSURLSearchParams, WebCore::URLSearchParams>(value);
}

// callback accepting a void* and a const ZigString*, returning void
typedef void (*URLSearchParams__toStringCallback)(void* ctx, const ZigString* str);

extern "C" void URLSearchParams__toString(WebCore::URLSearchParams* urlSearchParams, void* ctx, URLSearchParams__toStringCallback callback)
{
    String str = urlSearchParams->toString();
    auto zig = Zig::toZigString(str);
    callback(ctx, &zig);
}

URLSearchParams::URLSearchParams(Vector<KeyValuePair<String, String>>&& pairs, DOMURL* associatedURL)
    : m_associatedURL(associatedURL)
    , m_pairs(WTF::move(pairs))
{
}

URLSearchParams::URLSearchParams(const Vector<KeyValuePair<String, String>>& pairs)
    : m_pairs(pairs)
{
}

URLSearchParams::~URLSearchParams() = default;

ExceptionOr<Ref<URLSearchParams>> URLSearchParams::create(const String& string, DOMURL* associatedURL)
{
    auto parsed = tryParseURLEncodedForm(string.startsWith('?') ? StringView(string).substring(1) : StringView(string));
    if (parsed.hasException())
        return parsed.releaseException();
    return adoptRef(*new URLSearchParams(parsed.releaseReturnValue(), associatedURL));
}

ExceptionOr<Ref<URLSearchParams>> URLSearchParams::create(std::variant<Vector<Vector<String>>, Vector<KeyValuePair<String, String>>, String>&& variant)
{
    const size_t maxPairs = Bun__stringSyntheticAllocationLimit / sizeof(KeyValuePair<String, String>);
    auto visitor = WTF::makeVisitor([&](const Vector<Vector<String>>& vector) -> ExceptionOr<Ref<URLSearchParams>> {
        if (vector.size() > maxPairs) [[unlikely]]
            return Exception { OutOfMemoryError };
        Vector<KeyValuePair<String, String>> pairs;
        if (!pairs.tryReserveInitialCapacity(vector.size())) [[unlikely]]
            return Exception { OutOfMemoryError };
        for (const auto& pair : vector) {
            if (pair.size() != 2)
                return Exception { TypeError };
            pairs.unsafeAppendWithoutCapacityCheck({ pair[0], pair[1] });
        }
        return adoptRef(*new URLSearchParams(WTF::move(pairs), nullptr)); }, [&](const Vector<KeyValuePair<String, String>>& pairs) -> ExceptionOr<Ref<URLSearchParams>> {
        if (pairs.size() > maxPairs) [[unlikely]]
            return Exception { OutOfMemoryError };
        return adoptRef(*new URLSearchParams(pairs)); }, [&](const String& string) -> ExceptionOr<Ref<URLSearchParams>> { return URLSearchParams::create(string, nullptr); });
    return std::visit(visitor, variant);
}

String URLSearchParams::get(const StringView name) const
{
    for (const auto& pair : m_pairs) {
        if (pair.key == name)
            return pair.value;
    }
    return String();
}

bool URLSearchParams::has(const StringView name, const String& value) const
{
    for (const auto& pair : m_pairs) {
        if (pair.key == name && (value.isNull() || pair.value == value))
            return true;
    }
    return false;
}

void URLSearchParams::sort()
{
    std::stable_sort(m_pairs.begin(), m_pairs.end(), [](const auto& a, const auto& b) {
        return WTF::codePointCompareLessThan(a.key, b.key);
    });
    updateURL();
    needsSorting = false;
}

ExceptionOr<void> URLSearchParams::set(const String& name, const String& value)
{
    for (auto& pair : m_pairs) {
        if (pair.key != name)
            continue;
        if (pair.value != value)
            pair.value = value;
        bool skippedFirstMatch = false;
        m_pairs.removeAllMatching([&](const auto& pair) {
            if (pair.key == name) {
                if (skippedFirstMatch)
                    return true;
                skippedFirstMatch = true;
            }
            return false;
        });
        updateURL();
        needsSorting = true;
        return {};
    }
    if (m_pairs.size() >= Bun__stringSyntheticAllocationLimit / sizeof(KeyValuePair<String, String>)) [[unlikely]]
        return Exception { OutOfMemoryError };
    if (!m_pairs.tryAppend({ name, value })) [[unlikely]]
        return Exception { OutOfMemoryError };
    needsSorting = true;
    updateURL();
    return {};
}

ExceptionOr<void> URLSearchParams::append(const String& name, const String& value)
{
    if (m_pairs.size() >= Bun__stringSyntheticAllocationLimit / sizeof(KeyValuePair<String, String>)) [[unlikely]]
        return Exception { OutOfMemoryError };
    if (!m_pairs.tryAppend({ name, value })) [[unlikely]]
        return Exception { OutOfMemoryError };
    updateURL();
    needsSorting = true;
    return {};
}

Vector<String> URLSearchParams::getAll(const StringView name) const
{
    Vector<String> values;
    values.reserveInitialCapacity(m_pairs.size());
    for (const auto& pair : m_pairs) {
        if (pair.key == name)
            values.unsafeAppendWithoutCapacityCheck(pair.value);
    }
    values.shrinkToFit();
    return values;
}

void URLSearchParams::remove(const StringView name, const String& value)
{
    m_pairs.removeAllMatching([&](const auto& pair) {
        return pair.key == name && (value.isNull() || pair.value == value);
    });
    updateURL();
    needsSorting = true;
}

String URLSearchParams::toString() const
{
    return WTF::URLParser::serialize(m_pairs);
}

void URLSearchParams::updateURL()
{
    if (m_associatedURL)
        m_associatedURL->markSearchParamsDirty();
}

ExceptionOr<void> URLSearchParams::updateFromAssociatedURL()
{
    ASSERT(m_associatedURL);
    m_pairs.clear();
    String search = m_associatedURL->search();
    auto parsed = tryParseURLEncodedForm(search.startsWith('?') ? StringView(search).substring(1) : StringView(search));
    if (parsed.hasException())
        return parsed.releaseException();
    m_pairs = parsed.releaseReturnValue();
    return {};
}

std::optional<KeyValuePair<String, String>> URLSearchParams::Iterator::next()
{
    auto& pairs = m_target->pairs();
    if (m_index >= pairs.size())
        return std::nullopt;

    auto& pair = pairs[m_index++];
    return KeyValuePair<String, String> { pair.key, pair.value };
}

URLSearchParams::Iterator::Iterator(URLSearchParams& params)
    : m_target(params)
{
}

size_t URLSearchParams::memoryCost() const
{
    size_t cost = sizeof(URLSearchParams);
    for (const auto& pair : m_pairs) {
        cost += pair.key.sizeInBytes();
        cost += pair.value.sizeInBytes();
    }
    return cost;
}
}
