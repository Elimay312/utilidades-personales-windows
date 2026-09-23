#include <doctest/doctest.h>

#include "ui/controls.h"
#include "ui/spring.h"

using namespace panel;

namespace {

PanelState Sample() {
  PanelState state;
  state.displays = {DisplayState{L"Portátil", L"a", 0.5f, true, true, true},
                    DisplayState{L"LG", L"b", 0.5f, false, true, false},
                    DisplayState{L"DELL", L"c", 0.5f, false, false, false}};
  state.audio.outputs.resize(3);
  state.apps = {AppEntry{L"Dock", 0, true, true}, AppEntry{L"Lanzador", 0, false, false}};
  return state;
}

D2D1_POINT_2F Center(const D2D1_RECT_F& rect) {
  return D2D1_POINT_2F{(rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f};
}

Target HitAt(const PanelLayout& layout, const PanelState& state, const D2D1_RECT_F& rect) {
  const D2D1_POINT_2F point = Center(rect);
  return HitTest(layout, state, point.x, point.y);
}

}  // namespace

TEST_CASE("a click on a slider puts the end of the fill under the pointer") {
  const D2D1_RECT_F slider{10.0f, 0.0f, 310.0f, 28.0f};  // 300 wide, 272 of travel
  CHECK(SliderValueAt(slider, 10.0f + 14.0f) == doctest::Approx(0.0f));
  CHECK(SliderValueAt(slider, 310.0f - 14.0f) == doctest::Approx(1.0f));
  CHECK(SliderValueAt(slider, 10.0f + 14.0f + 136.0f) == doctest::Approx(0.5f));
  // Past either end it pins instead of running off: a drag that overshoots stays in range.
  CHECK(SliderValueAt(slider, -500.0f) == 0.0f);
  CHECK(SliderValueAt(slider, 5000.0f) == 1.0f);
}

TEST_CASE("the mouse finds what is drawn where it points") {
  const PanelState state = Sample();
  const PanelLayout closed = MakeLayout(Expanded{}, state);
  for (size_t i = 0; i < 4; ++i) CHECK(HitAt(closed, state, closed.tiles[i]) == Target{Part::Tile, i});
  CHECK(HitAt(closed, state, closed.brightnessHeader) == Target{Part::BrightnessHeader});
  CHECK(HitAt(closed, state, closed.brightnessSlider) == Target{Part::BrightnessSlider});
  CHECK(HitAt(closed, state, closed.audioSlider) == Target{Part::VolumeSlider});
  CHECK(HitAt(closed, state, MuteRect(closed.audioSlider)) == Target{Part::Mute});
  CHECK(HitAt(closed, state, closed.apps[0]) == Target{Part::App, 0});
  // A utility that is not installed is drawn, faded, and does nothing.
  CHECK(HitAt(closed, state, closed.apps[1]).empty());
  // The gap between two tiles is nothing.
  CHECK(HitTest(closed, state, closed.tiles[0].right + 1.0f, Center(closed.tiles[0]).y).empty());
}

TEST_CASE("open cards hit their rows, and a screen without DDC/CI is not a slider") {
  const PanelState state = Sample();
  const PanelLayout open = MakeLayout(Expanded{1.0f, 1.0f}, state);
  CHECK(HitAt(open, state, open.displayRows[0].slider) == Target{Part::DisplaySlider, 0});
  CHECK(HitAt(open, state, open.displayRows[1].slider) == Target{Part::DisplaySlider, 1});
  CHECK(HitAt(open, state, open.displayRows[2].slider).empty());
  CHECK(HitAt(open, state, open.outputRows[2]) == Target{Part::Output, 2});
}

TEST_CASE("a row still sliding into its card cannot be clicked yet") {
  const PanelState state = Sample();
  const PanelLayout half = MakeLayout(Expanded{0.3f, 0.3f}, state);
  CHECK(HitAt(half, state, half.displayRows[2].slider).empty());
  CHECK(HitAt(half, state, half.outputRows[2]).empty());
}

TEST_CASE("Tab walks everything in reading order and wraps") {
  const PanelState state = Sample();
  const PanelLayout closed = MakeLayout(Expanded{}, state);
  const std::vector<Target> order = FocusOrder(closed, state);
  const std::vector<Target> expected{
      {Part::Tile, 0},        {Part::Tile, 1},    {Part::Tile, 2}, {Part::Tile, 3},
      {Part::BrightnessHeader}, {Part::BrightnessSlider}, {Part::AudioHeader},
      {Part::VolumeSlider},   {Part::App, 0}};
  CHECK(order == expected);
  CHECK(NextFocus(order, Target{}, false) == order.front());
  CHECK(NextFocus(order, Target{}, true) == order.back());
  CHECK(NextFocus(order, order.back(), false) == order.front());
  CHECK(NextFocus(order, order.front(), true) == order.back());

  const PanelLayout open = MakeLayout(Expanded{1.0f, 1.0f}, state);
  const std::vector<Target> openOrder = FocusOrder(open, state);
  // Open: the reachable screens instead of the one slider, and the three outputs.
  CHECK(std::count(openOrder.begin(), openOrder.end(), Target{Part::DisplaySlider, 0}) == 1);
  CHECK(std::count(openOrder.begin(), openOrder.end(), Target{Part::DisplaySlider, 2}) == 0);
  CHECK(std::count(openOrder.begin(), openOrder.end(), Target{Part::BrightnessSlider}) == 0);
  CHECK(openOrder.size() == expected.size() - 1 + 2 + 3);
}

TEST_CASE("the spring reaches its target, stops there, and can be turned around mid-way") {
  Spring spring;
  int frames = 0;
  while (spring.Step(1.0f / 60.0f, 1.0f) && frames < 600) ++frames;
  CHECK(spring.resting(1.0f));
  // A 250 ms period: well under a second to rest, and not in a single frame either.
  CHECK(frames > 6);
  CHECK(frames < 60);

  // Interrupted: it turns back from where it is, with the speed it had, without a jump.
  Spring back;
  for (int i = 0; i < 5; ++i) back.Step(1.0f / 60.0f, 1.0f);
  const float before = back.x;
  back.Step(1.0f / 60.0f, 0.0f);
  CHECK(back.x > 0.0f);
  CHECK(std::abs(back.x - before) < 0.1f);
}
