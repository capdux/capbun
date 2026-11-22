import css from "../css_parser.zig";

const LengthPercentage = css.css_values.length.LengthPercentage;
const CssColor = css.css_values.color.CssColor;
const Url = css.css_values.url.Url;

struct SVGPaintUrl {
    /// The url of the paint server.
    url: Url,
    /// A fallback to be used in case the paint server cannot be resolved.
    fallback: SVGPaintFallback?,
}

/// An SVG [`<paint>`](https://www.w3.org/TR/SVG2/painting.html#SpecifyingPaint) value
/// used in the `fill` and `stroke` properties.
union(enum) SVGPaint {
    /// A URL reference to a paint server element, e.g. `linearGradient`, `radialGradient`, and `pattern`.
    Url: SVGPaintUrl,
    /// A solid color paint.
    Color: CssColor,
    /// Use the paint value of fill from a context element.
    ContextFill: void,
    /// Use the paint value of stroke from a context element.
    ContextStroke: void,
    /// No paint.
    None: void,
}

/// A fallback for an SVG paint in case a paint server `url()` cannot be resolved.
///
/// See [SVGPaint](SVGPaint).
union(enum) SVGPaintFallback {
    /// No fallback.
    None: void,
    /// A solid color.
    Color: CssColor,
}

/// A value for the [stroke-linecap](https://www.w3.org/TR/SVG2/painting.html#LineCaps) property.
pub const StrokeLinecap = css.DefineEnumProperty(@compileError(css.todo_stuff.depth));

/// A value for the [stroke-linejoin](https://www.w3.org/TR/SVG2/painting.html#LineJoin) property.
pub const StrokeLinejoin = css.DefineEnumProperty(@compileError(css.todo_stuff.depth));

/// A value for the [stroke-dasharray](https://www.w3.org/TR/SVG2/painting.html#StrokeDashing) property.
union(enum) StrokeDasharray {
    /// No dashing is used.
    None: void,
    /// Specifies a dashing pattern to use.
    Values: ArrayList(LengthPercentage),
}

/// A value for the [marker](https://www.w3.org/TR/SVG2/painting.html#VertexMarkerProperties) properties.
union(enum) Marker {
    /// No marker.
    None: void,
    /// A url reference to a `<marker>` element.
    Url: Url,
}

/// A value for the [color-interpolation](https://www.w3.org/TR/SVG2/painting.html#ColorInterpolation) property.
pub const ColorInterpolation = css.DefineEnumProperty(@compileError(css.todo_stuff.depth));

/// A value for the [color-rendering](https://www.w3.org/TR/SVG2/painting.html#ColorRendering) property.
pub const ColorRendering = css.DefineEnumProperty(@compileError(css.todo_stuff.depth));

/// A value for the [shape-rendering](https://www.w3.org/TR/SVG2/painting.html#ShapeRendering) property.
pub const ShapeRendering = css.DefineEnumProperty(@compileError(css.todo_stuff.depth));

/// A value for the [text-rendering](https://www.w3.org/TR/SVG2/painting.html#TextRendering) property.
pub const TextRendering = css.DefineEnumProperty(@compileError(css.todo_stuff.depth));

/// A value for the [image-rendering](https://www.w3.org/TR/SVG2/painting.html#ImageRendering) property.
pub const ImageRendering = css.DefineEnumProperty(@compileError(css.todo_stuff.depth));

const ArrayList = std.ArrayListUnmanaged;
