#include <doctest/doctest.h>

#include "ui/layout.h"

using namespace panel;

namespace {

PanelState Sample(size_t displays, size_t outputs, size_t apps) {
  PanelState state;
  state.displays.resize(displays);
  state.audio.outputs.resize(outputs);
  state.apps.resize(apps);
  return state;
}

bool Inside(const D2D1_RECT_F& inner, const D2D1_RECT_F& outer) {
  return inner.left >= outer.left && inner.right <= outer.right && inner.top >= outer.top &&
         inner.bottom <= outer.bottom;
}

}  // namespace

TEST_CASE("closed, the panel is the design's 344 x 392") {
  const PanelLayout layout = MakeLayout(Expanded{}, Sample(3, 3, 5));
  CHECK(layout.width == doctest::Approx(344.0f));
  CHECK(layout.height == doctest::Approx(392.0f));
  CHECK(layout.displayRows.empty());
  CHECK(layout.outputRows.empty());
}

TEST_CASE("nothing sticks out of the panel or overlaps the next section") {
  const PanelState state = Sample(3, 3, 5);
  for (const Expanded open : {Expanded{}, Expanded{1.0f, 0.0f}, Expanded{0.0f, 1.0f},
                              Expanded{1.0f, 1.0f}}) {
    const PanelLayout layout = MakeLayout(open, state);
    const D2D1_RECT_F whole{0.0f, 0.0f, layout.width, layout.height};
    for (const D2D1_RECT_F& tile : layout.tiles) CHECK(Inside(tile, whole));
    CHECK(layout.tiles[2].bottom < layout.brightnessCard.top);
    CHECK(layout.brightnessCard.bottom < layout.audioCard.top);
    CHECK(Inside(layout.brightnessHeader, layout.brightnessCard));
    for (const DisplayRow& row : layout.displayRows) {
      CHECK(Inside(row.slider, layout.brightnessCard));
      CHECK(row.label.bottom <= row.slider.top);
    }
    CHECK(Inside(layout.audioSlider, layout.audioCard));
    for (const D2D1_RECT_F& row : layout.outputRows) CHECK(Inside(row, layout.audioCard));
    REQUIRE(layout.apps.size() == 5);
    CHECK(layout.audioCard.bottom < layout.apps.front().top);
    CHECK(Inside(layout.apps.back(), whole));
    CHECK(layout.apps.back().bottom + kPadDip == doctest::Approx(layout.height));
  }
}

TEST_CASE("opening a card grows the panel by exactly what it shows") {
  const PanelState state = Sample(3, 3, 5);
  const float closed = MakeLayout(Expanded{}, state).height;
  const float brightness = MakeLayout(Expanded{1.0f, 0.0f}, state).height;
  const float audio = MakeLayout(Expanded{0.0f, 1.0f}, state).height;
  // Three rows instead of one slider.
  CHECK(brightness - closed ==
        doctest::Approx(3 * kDisplayRowDip + 2 * kDisplayRowGapDip - kSliderHeightDip));
  CHECK(audio - closed == doctest::Approx(kGapDip + 5.0f + 3 * kOutputRowDip - kPadDip + kGapDip / 2));
  CHECK(MakeLayout(Expanded{1.0f, 1.0f}, state).height ==
        doctest::Approx(closed + (brightness - closed) + (audio - closed)));
}

TEST_CASE("a card halfway open is halfway between, and the rest moves with it") {
  const PanelState state = Sample(3, 3, 5);
  const PanelLayout closed = MakeLayout(Expanded{}, state);
  const PanelLayout open = MakeLayout(Expanded{1.0f, 0.0f}, state);
  const PanelLayout half = MakeLayout(Expanded{0.5f, 0.0f}, state);
  CHECK(half.brightnessCard.bottom ==
        doctest::Approx((closed.brightnessCard.bottom + open.brightnessCard.bottom) / 2.0f));
  CHECK(half.audioCard.top - closed.audioCard.top ==
        doctest::Approx(half.brightnessCard.bottom - closed.brightnessCard.bottom));
  // The rows are laid out where they will end up, and the card clips them on the way.
  REQUIRE(half.displayRows.size() == 3);
  CHECK(half.displayRows[2].slider.top == doctest::Approx(open.displayRows[2].slider.top));
  CHECK(half.displayRows[2].slider.bottom > half.brightnessCard.bottom);
}

TEST_CASE("an open card with nothing to list stays closed") {
  const PanelState empty = Sample(0, 0, 5);
  CHECK(MakeLayout(Expanded{1.0f, 1.0f}, empty).height ==
        doctest::Approx(MakeLayout(Expanded{}, empty).height));
}

TEST_CASE("a notice adds one line under everything") {
  PanelState state = Sample(1, 1, 5);
  const float without = MakeLayout(Expanded{}, state).height;
  state.notice = L"Otra aplicación ya usa Ctrl+Alt+A.";
  const PanelLayout layout = MakeLayout(Expanded{}, state);
  CHECK(layout.height == doctest::Approx(without + kGapDip + kNoticeDip));
  CHECK(layout.notice.top > layout.apps.back().bottom);
}

TEST_CASE("the panel hugs the bottom right corner of the work area") {
  const RECT work{0, 0, 1920, 1040};
  const RECT at100 = PlaceRect(work, D2D1_SIZE_F{344.0f, 392.0f}, 96);
  CHECK(at100.right == 1920 - kMarginDip);
  CHECK(at100.bottom == 1040 - kMarginDip);
  CHECK(at100.right - at100.left == 344);
  CHECK(at100.bottom - at100.top == 392);

  // At 125 % every DIP is 1.25 px, the margin included, and it still lands in the corner.
  const RECT offset{2560, 0, 4480, 1040};
  const RECT at125 = PlaceRect(offset, D2D1_SIZE_F{344.0f, 392.0f}, 120);
  CHECK(at125.right == 4480 - 15);
  CHECK(at125.right - at125.left == 430);
  CHECK(at125.bottom - at125.top == 490);
}
