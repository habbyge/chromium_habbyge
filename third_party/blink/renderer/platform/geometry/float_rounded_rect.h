/*
 * Copyright (C) 2013 Adobe Systems Incorporated. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GEOMETRY_FLOAT_ROUNDED_RECT_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GEOMETRY_FLOAT_ROUNDED_RECT_H_

#include <iosfwd>
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/platform/geometry/float_rect.h"
#include "third_party/blink/renderer/platform/geometry/float_size.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/skia/include/core/SkRRect.h"

namespace blink {

class FloatQuad;

// Represents a rect with rounded corners.
// We don't use gfx::RRect in blink because gfx::RRect is based on SkRRect
// which always keeps the radii constrained within the size of the rect, but
// in blink sometimes we need to keep the unconstrained status of a rounded
// rect. See ConstrainRadii(). This class also provides functions that are
// uniquely needed by blink.
class PLATFORM_EXPORT FloatRoundedRect {
  DISALLOW_NEW();

 public:
  class PLATFORM_EXPORT Radii {
    DISALLOW_NEW();

   public:
    constexpr Radii() = default;
    constexpr Radii(const FloatSize& top_left,
                    const FloatSize& top_right,
                    const FloatSize& bottom_left,
                    const FloatSize& bottom_right)
        : top_left_(top_left),
          top_right_(top_right),
          bottom_left_(bottom_left),
          bottom_right_(bottom_right) {}
    explicit constexpr Radii(float radius) : Radii(radius, radius) {}
    constexpr Radii(float radius_x, float radius_y)
        : Radii(FloatSize(radius_x, radius_y),
                FloatSize(radius_x, radius_y),
                FloatSize(radius_x, radius_y),
                FloatSize(radius_x, radius_y)) {}

    constexpr Radii(const Radii&) = default;
    constexpr Radii& operator=(const Radii&) = default;

    void SetTopLeft(const FloatSize& size) { top_left_ = size; }
    void SetTopRight(const FloatSize& size) { top_right_ = size; }
    void SetBottomLeft(const FloatSize& size) { bottom_left_ = size; }
    void SetBottomRight(const FloatSize& size) { bottom_right_ = size; }
    constexpr const FloatSize& TopLeft() const { return top_left_; }
    constexpr const FloatSize& TopRight() const { return top_right_; }
    constexpr const FloatSize& BottomLeft() const { return bottom_left_; }
    constexpr const FloatSize& BottomRight() const { return bottom_right_; }

    void SetMinimumRadius(float);
    absl::optional<float> UniformRadius() const;

    constexpr bool IsZero() const {
      return top_left_.IsZero() && top_right_.IsZero() &&
             bottom_left_.IsZero() && bottom_right_.IsZero();
    }

    void Scale(float factor);

    void Expand(float top_width,
                float bottom_width,
                float left_width,
                float right_width);
    void Expand(float size) { Expand(size, size, size, size); }

    void Shrink(float top_width,
                float bottom_width,
                float left_width,
                float right_width);
    void Shrink(float size) { Shrink(size, size, size, size); }

    String ToString() const;

   private:
    FloatSize top_left_;
    FloatSize top_right_;
    FloatSize bottom_left_;
    FloatSize bottom_right_;
  };

  constexpr FloatRoundedRect() = default;
  explicit FloatRoundedRect(const gfx::RectF& rect,
                            const Radii& radii = Radii())
      : FloatRoundedRect(FloatRect(rect), radii) {}
  explicit FloatRoundedRect(const FloatRect&, const Radii& = Radii());
  explicit FloatRoundedRect(const IntRect&, const Radii& = Radii());
  FloatRoundedRect(float x, float y, float width, float height);
  FloatRoundedRect(const FloatRect&,
                   const FloatSize& top_left,
                   const FloatSize& top_right,
                   const FloatSize& bottom_left,
                   const FloatSize& bottom_right);
  FloatRoundedRect(const FloatRect& r, float radius)
      : FloatRoundedRect(r, Radii(radius)) {}
  FloatRoundedRect(const FloatRect& r, float radius_x, float radius_y)
      : FloatRoundedRect(r, Radii(radius_x, radius_y)) {}

  constexpr const FloatRect& Rect() const { return rect_; }
  constexpr const Radii& GetRadii() const { return radii_; }
  constexpr bool IsRounded() const { return !radii_.IsZero(); }
  constexpr bool IsEmpty() const { return rect_.IsEmpty(); }

  void SetRect(const FloatRect& rect) { rect_ = rect; }
  void SetRadii(const Radii& radii) { radii_ = radii; }

  void Move(const FloatSize& size) { rect_.Offset(size); }
  void InflateWithRadii(int size);
  void Inflate(float size) { rect_.Outset(size); }

  // expandRadii() does not have any effect on corner radii which have zero
  // width or height. This is because the process of expanding the radius of a
  // corner is not allowed to make sharp corners non-sharp. This applies when
  // "spreading" a shadow or a box shape.
  void ExpandRadii(float size) { radii_.Expand(size); }
  void ShrinkRadii(float size) { radii_.Shrink(size); }

  // Returns a quickly computed rect enclosed by the rounded rect.
  FloatRect RadiusCenterRect() const;

  constexpr FloatRect TopLeftCorner() const {
    return FloatRect(rect_.x(), rect_.y(), radii_.TopLeft().width(),
                     radii_.TopLeft().height());
  }
  constexpr FloatRect TopRightCorner() const {
    return FloatRect(rect_.right() - radii_.TopRight().width(), rect_.y(),
                     radii_.TopRight().width(), radii_.TopRight().height());
  }
  constexpr FloatRect BottomLeftCorner() const {
    return FloatRect(rect_.x(), rect_.bottom() - radii_.BottomLeft().height(),
                     radii_.BottomLeft().width(), radii_.BottomLeft().height());
  }
  constexpr FloatRect BottomRightCorner() const {
    return FloatRect(rect_.right() - radii_.BottomRight().width(),
                     rect_.bottom() - radii_.BottomRight().height(),
                     radii_.BottomRight().width(),
                     radii_.BottomRight().height());
  }

  bool XInterceptsAtY(float y,
                      float& min_x_intercept,
                      float& max_x_intercept) const;

  // Tests whether the quad intersects any part of this rounded rectangle.
  // This only works for convex quads.
  // This intersection is edge-inclusive and will return true even if the
  // intersecting area is empty (i.e., the intersection is a line or a point).
  bool IntersectsQuad(const FloatQuad&) const;

  // Whether the radii are constrained in the size of rect().
  bool IsRenderable() const;

  // Constrains the radii to be no bigger than the size of rect().
  // This is not called automatically in this class because sometimes we want
  // to keep the !IsRenderable() status, e.g. for a rounded inner border edge
  // that is shrunk from a rounded outer border edge to keep uniform width of
  // the rounded border.
  void ConstrainRadii();

  operator SkRRect() const;

  String ToString() const;

 private:
  FloatRect rect_;
  Radii radii_;
};

inline FloatRoundedRect::operator SkRRect() const {
  SkRRect rrect;

  if (IsRounded()) {
    SkVector radii[4];
    radii[SkRRect::kUpperLeft_Corner].set(TopLeftCorner().width(),
                                          TopLeftCorner().height());
    radii[SkRRect::kUpperRight_Corner].set(TopRightCorner().width(),
                                           TopRightCorner().height());
    radii[SkRRect::kLowerRight_Corner].set(BottomRightCorner().width(),
                                           BottomRightCorner().height());
    radii[SkRRect::kLowerLeft_Corner].set(BottomLeftCorner().width(),
                                          BottomLeftCorner().height());

    rrect.setRectRadii(Rect(), radii);
  } else {
    rrect.setRect(Rect());
  }

  return rrect;
}

constexpr bool operator==(const FloatRoundedRect::Radii& a,
                          const FloatRoundedRect::Radii& b) {
  return a.TopLeft() == b.TopLeft() && a.TopRight() == b.TopRight() &&
         a.BottomLeft() == b.BottomLeft() && a.BottomRight() == b.BottomRight();
}

constexpr bool operator!=(const FloatRoundedRect::Radii& a,
                          const FloatRoundedRect::Radii& b) {
  return !(a == b);
}

constexpr bool operator==(const FloatRoundedRect& a,
                          const FloatRoundedRect& b) {
  return a.Rect() == b.Rect() && a.GetRadii() == b.GetRadii();
}

constexpr bool operator!=(const FloatRoundedRect& a,
                          const FloatRoundedRect& b) {
  return !(a == b);
}

PLATFORM_EXPORT std::ostream& operator<<(std::ostream&,
                                         const FloatRoundedRect&);
PLATFORM_EXPORT std::ostream& operator<<(std::ostream&,
                                         const FloatRoundedRect::Radii&);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GEOMETRY_FLOAT_ROUNDED_RECT_H_
