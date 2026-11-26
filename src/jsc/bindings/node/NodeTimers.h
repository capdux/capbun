#pragma once

#include "root.h"

namespace Bun {

JSC_DECLARE_HOST_FUNCTION(functionSetTimeout);
JSC_DECLARE_HOST_FUNCTION(functionSetInterval);
JSC_DECLARE_HOST_FUNCTION(functionSetImmediate);

JSC::JSValue constructSetTimeout(JSC::VM&, JSC::JSObject*);
JSC::JSValue constructSetInterval(JSC::VM&, JSC::JSObject*);
JSC::JSValue constructSetImmediate(JSC::VM&, JSC::JSObject*);
JSC_DECLARE_HOST_FUNCTION(functionClearTimeout);
JSC_DECLARE_HOST_FUNCTION(functionClearInterval);
JSC_DECLARE_HOST_FUNCTION(functionClearImmediate);

} // namespace Bun
