#pragma once
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "paper_sheet.hpp"
#include "suite.hpp"
#include <functional>
namespace games {
// Visually a text link; retains keyboard and accessibility semantics.
class HelpGlyph final : public gf::Button {
  public:
    HelpGlyph(gf::StableId id, std::string glyph);
    void set_ink(gf::Color ink);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;

  private:
    gf::Color ink_ = gf::Color::rgba(184, 137, 42);
};
class HelpPages final : public gf::ScrollableControl {
  public:
    explicit HelpPages(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_key_bubble(gf::KeyEvent& event) override;
    void select(int entry, std::string_view topic);

  private:
    // The dedication, the credits and where to write stand above every topic.
    std::shared_ptr<gf::Label> dedication_, credits_, contact_;
    std::vector<std::shared_ptr<gf::Button>> headings_;
    std::vector<std::shared_ptr<gf::Label>> bodies_;
    struct TopicKey {
        int entry;
        std::string topic;
    };
    std::vector<TopicKey> topics_;
    std::vector<int> order_;
    // The topics are made for what help is open on: the shell's own pages and the
    // library's. Made again only when that changes.
    int scope_ = -2;
    void build(int scope);
    void add_topic(int entry, std::string topic, std::string title, std::string_view body);
    void toggle(int index);
};
class HelpBook final : public gf::Control {
  public:
    explicit HelpBook(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void select(int entry, std::string_view topic);
    void on_key_bubble(gf::KeyEvent& event) override;
    std::function<void()> close;

  private:
    std::shared_ptr<HelpPages> pages_;
    std::shared_ptr<HelpGlyph> close_;
    PaperSheet sheet_;
    void clicked_close();
    void on_detaching_from_window(gf::Window& window) noexcept override;
};
} // namespace games
