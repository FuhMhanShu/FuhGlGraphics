// Unit tests for the G41 2D UI layer (pure CPU — no GL context needed): Rect, Font
// metrics/wrap, Property/Binding (incl. loop-freedom), layouts, hit-testing, event
// dispatch/focus and the widget behaviors (Button, Checkbox, Slider, LineEdit, ListBox).

#include "test_framework.hpp"
#include "tests.hpp"

#include <fuhglgraphics/ui/ui_root.hpp>
#include <fuhglgraphics/ui/widgets.hpp>

#include <string>

using namespace fuhglgraphics::ui;

namespace {

Event mouseMove(float x, float y) {
    Event e;
    e.type = EventType::MouseMove;
    e.x = x;
    e.y = y;
    return e;
}
Event mouseBtn(float x, float y, bool down) {
    Event e;
    e.type = EventType::MouseButton;
    e.x = x;
    e.y = y;
    e.button = MouseButton::Left;
    e.down = down;
    return e;
}
Event wheelEv(float x, float y, float delta) {
    Event e;
    e.type = EventType::Wheel;
    e.x = x;
    e.y = y;
    e.wheelDelta = delta;
    return e;
}
Event keyEv(int key, int mods = 0) {
    Event e;
    e.type = EventType::Key;
    e.key = key;
    e.keyDown = true;
    e.modifiers = mods;
    return e;
}
Event charEv(const std::string& t) {
    Event e;
    e.type = EventType::Char;
    e.text = t;
    return e;
}
void click(UiRoot& ui, float x, float y) {
    ui.dispatch(mouseMove(x, y));
    ui.dispatch(mouseBtn(x, y, true));
    ui.dispatch(mouseBtn(x, y, false));
}

} // namespace

void runUiTests() {
    // --- Rect ---
    const Rect r{10.f, 20.f, 100.f, 50.f};
    CHECK(r.contains(10.f, 20.f));
    CHECK(r.contains(109.f, 69.f));
    CHECK(!r.contains(110.f, 70.f));
    CHECK(!r.contains(9.f, 20.f));
    CHECK((r.intersected(Rect{50.f, 40.f, 100.f, 100.f}) == Rect{50.f, 40.f, 60.f, 30.f}));
    CHECK(r.intersected(Rect{200.f, 200.f, 10.f, 10.f}).empty());
    CHECK((r.inset(5.f) == Rect{15.f, 25.f, 90.f, 40.f}));

    // --- Font metrics + wrap ---
    const Font f{2.f};
    CHECK_NEAR(f.cell(), 16.0, 1e-6);
    CHECK_NEAR(f.measure("abcd").x, 64.0, 1e-6);
    CHECK_NEAR(f.lineHeight(), 16.0, 1e-6);
    const auto words = f.wrap("aa bb cc", 3.5f * f.cell()); // 3.5 cells: "aa bb" (5) does not fit
    CHECK(words.size() == 3 && words[0] == "aa" && words[2] == "cc");
    const auto split = f.wrap("abcdef", 2.f * f.cell()); // hard-split overlong word
    CHECK(split.size() == 3 && split[0] == "ab" && split[2] == "ef");
    CHECK(f.wrap("", 100.f).size() == 1);

    // --- Property / Binding ---
    Property<int> a{1}, b{1};
    int fires = 0;
    a.onChanged().connect([&fires] { ++fires; });
    a.set(1);
    CHECK(fires == 0); // no change -> no signal
    a.set(2);
    CHECK(fires == 1 && a.get() == 2);
    bindTwoWay(a, b);
    a.set(5);
    CHECK(b.get() == 5);
    b.set(7);
    CHECK(a.get() == 7);
    CHECK(fires == 3); // set(2), set(5), write-back of b.set(7) — the loop terminates
    Property<std::string> src{"x"}, dst{"?"};
    bindOneWay(src, dst);
    CHECK(dst.get() == "x");
    src.set("y");
    CHECK(dst.get() == "y");
    src.set("z");
    CHECK(dst.get() == "z");

    // --- layouts + hit-testing ---
    UiRoot ui;
    ui.setSize(400, 300);
    auto& panel = ui.root.add<Panel>();
    panel.name = "panel";
    panel.rect = {10.f, 10.f, 220.f, 200.f};
    panel.title = "Settings";
    panel.setLayout(std::make_unique<VBoxLayout>());
    auto& btn = panel.add<Button>();
    btn.name = "ok";
    btn.text = "OK";
    auto& cb = panel.add<Checkbox>();
    cb.name = "opt";
    cb.label = "enable";
    auto& sl = panel.add<Slider>();
    sl.name = "vol";
    auto& le = panel.add<LineEdit>();
    le.name = "name";
    auto& lb = panel.add<ListBox>();
    lb.name = "list";
    lb.items = {"alpha", "beta", "gamma", "delta", "eps", "zeta", "eta", "theta"};
    ui.layout();
    CHECK((panel.abs == Rect{10.f, 10.f, 220.f, 200.f}));
    // VBox: children stacked top-down inside the panel content area, full width
    CHECK(btn.abs.y < cb.abs.y && cb.abs.y < sl.abs.y && sl.abs.y < le.abs.y && le.abs.y < lb.abs.y);
    CHECK(btn.abs.x >= panel.abs.x && btn.abs.right() <= panel.abs.right());
    CHECK(btn.abs.w > 100.f);
    // hit-testing returns the deepest widget
    CHECK(ui.root.hitTest(btn.abs.center().x, btn.abs.center().y) == &btn);
    CHECK(ui.root.hitTest(cb.abs.center().x, cb.abs.center().y) == &cb);
    CHECK(ui.root.hitTest(2.f, 2.f) == &ui.root); // outside the panel -> root

    // --- HBox / Grid ---
    UiRoot ui2;
    ui2.setSize(400, 300);
    auto& row = ui2.root.add<Container>();
    row.rect = {0.f, 0.f, 400.f, 40.f};
    row.setLayout(std::make_unique<HBoxLayout>());
    auto& b1 = row.add<Button>();
    auto& b2 = row.add<Button>();
    b1.sizeHint.prefW = 50.f;
    b2.sizeHint.prefW = 50.f;
    ui2.layout();
    CHECK(b1.abs.right() <= b2.abs.x); // side by side
    UiRoot ui3;
    ui3.setSize(400, 300);
    auto& grid = ui3.root.add<Container>();
    grid.rect = {0.f, 0.f, 400.f, 300.f};
    auto gl = std::make_unique<GridLayout>();
    gl->columns = 2;
    grid.setLayout(std::move(gl));
    auto& g0 = grid.add<Label>();
    auto& g1 = grid.add<Label>();
    auto& g2 = grid.add<Label>();
    ui3.layout();
    CHECK(g1.abs.x > g0.abs.x && g1.abs.y == g0.abs.y); // same row, next column
    CHECK(g2.abs.y > g0.abs.y && g2.abs.x == g0.abs.x); // second row, first column

    // --- Button click + focus ---
    int clicks = 0;
    btn.clicked.connect([&clicks] { ++clicks; });
    click(ui, btn.abs.center().x, btn.abs.center().y);
    CHECK(clicks == 1);
    CHECK(ui.focused() == &btn); // focusable widget got the focus on mouse-down
    click(ui, 2.f, 2.f);         // click on the root background
    CHECK(clicks == 1);          // no click
    CHECK(ui.focused() == nullptr);

    // --- Checkbox + two-way binding to a "backend" property ---
    Property<bool> model{false};
    bindTwoWay(model, cb.checked);
    int toggles = 0;
    cb.toggled.connect([&toggles] { ++toggles; });
    click(ui, cb.abs.center().x, cb.abs.center().y);
    CHECK(cb.checked.get() && model.get() && toggles == 1);
    model.set(false); // backend -> GUI
    CHECK(!cb.checked.get());
    CHECK(toggles == 2);

    // --- Slider drag ---
    ui.dispatch(mouseBtn(sl.abs.x + sl.abs.w * 0.75f, sl.abs.center().y, true));
    ui.dispatch(mouseMove(sl.abs.x + sl.abs.w * 0.75f, sl.abs.center().y));
    ui.dispatch(mouseBtn(sl.abs.x + sl.abs.w * 0.75f, sl.abs.center().y, false));
    CHECK_NEAR(sl.value.get(), 0.75, 0.02);
    ui.dispatch(keyEv(KeyLeft)); // arrows adjust the focused? slider is not focused; dispatch goes to focused (nullptr) — set focus first
    ui.focus(&sl);
    ui.dispatch(keyEv(KeyLeft));
    CHECK(sl.value.get() < 0.75f);

    // --- LineEdit: typing, editing keys, commit, validator ---
    int commits = 0;
    le.committed.connect([&commits] { ++commits; });
    click(ui, le.abs.center().x, le.abs.center().y);
    CHECK(ui.focused() == &le);
    ui.dispatch(charEv("hi"));
    CHECK(le.text.get() == "hi" && le.caret == 2);
    ui.dispatch(keyEv(KeyLeft));
    ui.dispatch(charEv("X"));
    CHECK(le.text.get() == "hXi");
    ui.dispatch(keyEv(KeyBackspace));
    CHECK(le.text.get() == "hi");
    ui.dispatch(keyEv(KeyHome));
    ui.dispatch(keyEv(KeyRight));
    ui.dispatch(keyEv(KeyDelete)); // removes 'i' at caret 1 -> "h"
    CHECK(le.text.get() == "h");
    ui.dispatch(keyEv(KeyEnter));
    CHECK(commits == 1);
    le.validator = [](const std::string& s) { return s.size() <= 3; };
    ui.dispatch(charEv("abcd")); // "habcd" (5) > 3 -> rejected
    CHECK(le.text.get() == "h");
    CHECK(!le.valid);
    ui.dispatch(keyEv(KeyEnd));
    ui.dispatch(keyEv(KeyBackspace));
    ui.dispatch(charEv("ok")); // "ok" passes
    CHECK(le.text.get() == "ok" && le.valid);
    // clipboard via UiRoot callbacks
    std::string clip;
    ui.clipboardSet = [&clip](const std::string& s) { clip = s; };
    ui.focus(&le);
    ui.dispatch(keyEv('a', ModCtrl)); // select all
    ui.dispatch(keyEv('c', ModCtrl)); // copy
    CHECK(clip == "ok");

    // --- ListBox: click select + wheel scroll ---
    ui.dispatch(mouseMove(lb.abs.center().x, lb.abs.y + 1.5f * lb.itemHeight));
    ui.dispatch(mouseBtn(lb.abs.center().x, lb.abs.y + 1.5f * lb.itemHeight, true));
    ui.dispatch(mouseBtn(lb.abs.center().x, lb.abs.y + 1.5f * lb.itemHeight, false));
    CHECK(lb.selected.get() == 1);
    ui.dispatch(wheelEv(lb.abs.center().x, lb.abs.center().y, -1.f)); // wheel down -> scroll down
    CHECK(lb.scroll > 0.f);
    ui.dispatch(wheelEv(lb.abs.center().x, lb.abs.center().y, 100.f));
    CHECK(lb.scroll == 0.f); // clamped at the top

    // --- Tab focus cycling ---
    ui.focus(nullptr);
    ui.dispatch(keyEv(KeyTab));
    CHECK(ui.focused() != nullptr);
    Widget* first = ui.focused();
    ui.dispatch(keyEv(KeyTab));
    CHECK(ui.focused() != nullptr && ui.focused() != first);
    ui.dispatch(keyEv(KeyEscape));
    CHECK(ui.focused() == nullptr);
}
