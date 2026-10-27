#include "help_route.hpp"
#include "pocketlibrary_view.hpp"

#include "game_data.hpp"
#include "platform/audio.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>

namespace pl {
namespace {

constexpr std::string_view game_id = "pocketlibrary";

// The library's parts, as books/parts.txt lists them (one file a line, the base first),
// and its shelf index, books/shelf.txt (library.hpp).
std::vector<std::vector<std::uint8_t>> library_parts() {
    std::vector<std::vector<std::uint8_t>> parts;
    const std::optional<std::string> list = games::load_game_asset("pocketlibrary/books/parts.txt");
    if (!list)
        return parts;
    std::size_t start = 0;
    while (start < (*list).size()) {
        std::size_t end = (*list).find('\n', start);
        if (end == std::string::npos)
            end = (*list).size();
        const std::string name = (*list).substr(start, end - start);
        start = end + 1;
        if (name.empty())
            continue;
        const std::optional<std::string> loaded = games::load_game_asset("pocketlibrary/books/" + name);
        std::vector<std::uint8_t> bytes;
        if (loaded)
            bytes.assign((*loaded).begin(), (*loaded).end());
        parts.push_back(std::move(bytes));
    }
    return parts;
}

std::string shelf_index() {
    const std::optional<std::string> loaded = games::load_game_asset("pocketlibrary/books/shelf.txt");
    return loaded ? *loaded : std::string();
}

const char* step_names[] = {"Small", "Medium", "Large"};

// The About card: what the book is, the edition it is set from, and on what terms.
std::vector<std::string> about_paragraphs(const ShelfBook& book) {
    std::vector<std::string> lines;
    lines.push_back(book.title);
    if (!book.subtitle.empty())
        lines.push_back(book.subtitle);
    if (!book.edition.empty())
        lines.push_back("Set from: " + book.edition + ".");
    std::string licence = "Licence: " + book.licence;
    if (!book.licence_url.empty())
        licence += " (" + book.licence_url + ")";
    lines.push_back(licence + ".");
    if (!book.credit.empty())
        lines.push_back("Credit: " + book.credit);
    if (book.share_alike())
        lines.push_back("You may copy and adapt this text, crediting its makers, and must share what you make "
                        "from it under the same licence. It stands with the other share-alike books at the end "
                        "of its shelf, and nothing of it is mixed into any other book.");
    else if (book.licence.find("-NC") != std::string::npos)
        lines.push_back("You may copy and adapt this text for any purpose that is not commercial, crediting its "
                        "makers. The Pocket Library is free and is never sold.");
    else if (book.licence.starts_with("CC BY"))
        lines.push_back("You may copy and adapt this text for any purpose, crediting its makers.");
    else
        lines.push_back("The text is in the public domain in the United States: free to read, copy and share.");
    lines.push_back("Press Escape or I to close this card.");
    return lines;
}

}  // namespace

PocketLibraryView::PocketLibraryView(gf::StableId id, Options options)
    : Control(std::move(id)), options_(options) {
    front_ = !options_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    gf::SurfaceMaterial plain;
    plain.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
    set_authored_surface_material(plain);
    if (library_.open(shelf_index(), library_parts())) {
        try {
            preparing_ = std::make_unique<gf::Worker>(prepare_library, this);
        } catch (...) {
            // Without a worker the first book opened prepares the library itself.
        }
    }
    const std::optional<std::string> saved = games::load_game_data(game_id, save_name());
    if (saved)
        static_cast<void>(decode_bookmarks(*saved, marks_));
    step_ = static_cast<page::TypeStep>(marks_.step);
    // The book open when the library was last left opens again where it was.
    if (!marks_.open.empty()) {
        for (std::size_t n = 0; n < library_.shelf().size(); ++n)
            if (library_.shelf()[n].id == marks_.open)
                open_book(static_cast<int>(n));
    }
    describe();
}

PocketLibraryView::~PocketLibraryView() {
    finish_preparing();
}

void PocketLibraryView::initialize_control_tree() {
    // The search: a GUI.Forms field over the bookcase's top, right of the capsule.
    search_ = gf::make_control<gf::TextBox>(gf::StableId("pocketlibrary.search"), std::string());
    (*search_).set_placeholder_text("Search titles and authors");
    (*search_).set_maximum_length(60);
    (*search_).set_font({gf::FontRole::content, 14, 400, false});
    (*search_).set_accessible_name("Search the library by title or author");
    (*search_).set_paint_plane(gf::PaintPlane::overlay);
    (*search_).set_visible(!reader_.is_open());
    add_child(search_);
    gf::on((*search_).text_changed(), *this, &PocketLibraryView::searched);
    gf::on((*search_).committed(), *this, &PocketLibraryView::search_committed);
    gf::on((*search_).cancelled(), *this, &PocketLibraryView::search_cancelled);
}

void PocketLibraryView::prepare_library(const gf::CancellationFlag&, void* context) {
    (*static_cast<PocketLibraryView*>(context)).library_.prepare();
}

void PocketLibraryView::finish_preparing() noexcept {
    if (preparing_) {
        (*preparing_).join();
        preparing_.reset();
    }
}

void PocketLibraryView::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subscriptions_.push_back((*timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<PocketLibraryView, &PocketLibraryView::tick>(*this)));
    request_frame();
}

void PocketLibraryView::on_detaching_from_window(gf::Window&) noexcept {
    try {
        persist();
    } catch (...) {
    }
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    finish_preparing();
    audio_stop();
}

void PocketLibraryView::activate() {
    gf::Window* window = attached_window();
    if (window != nullptr)
        static_cast<void>((*window).request_focus(shared_from_this()));
    request_frame();
}

void PocketLibraryView::set_cabinet(bool foreground, bool music, bool sound, bool) {
    front_ = foreground;
    audio_cabinet(foreground, music, sound);
    if (!foreground) {
        hover_ = -1;
        if (timer_)
            (*timer_).stop();
        return;
    }
    request_frame();
}

void PocketLibraryView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    gf::Window* window = attached_window();
    scale_ = window != nullptr ? (*window).scale() : 1.0;
    width_ = bounds.width;
    height_ = bounds.height;
    device_width_ = std::max(1, static_cast<int>(std::lround(bounds.width * scale_)));
    device_height_ = std::max(1, static_cast<int>(std::lround(bounds.height * scale_)));
    frame_.resize(device_width_, device_height_);
    lay_out_bookcase();
    place_search();
    if (surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
        static_cast<void>((*surface_).reconfigure(description));
    }
    request_frame();
}

void PocketLibraryView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (surface_)
        painter.draw_live_surface(surface_, bounds);
    else
        painter.fill_rect(bounds, gf::Color::rgba(37, 23, 13));
}

// Asks for one frame. Safe to call often: the timer fires once and stops.
void PocketLibraryView::request_frame() {
    render_dirty_ = true;
    if (!timer_ || !visible() || !front_)
        return;
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}

void PocketLibraryView::tick() {
    // The sound: a page's rustle is loaded the first time it is wanted and only starts
    // when the audio is ticked, so the timer runs while any sound is loading or fading.
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_tick_).count();
    last_tick_ = now;
    audio_tick(elapsed);
    if (!visible() || !front_) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    if (render_dirty_) {
        render_dirty_ = false;
        render();
        publish();
    }
    if (timer_ && !render_dirty_ && !audio_needs_tick())
        (*timer_).stop();
}

// Keeps the timer running for the sound, without drawing a frame.
void PocketLibraryView::wake_audio() {
    if (!timer_ || !visible() || !front_)
        return;
    if (!(*timer_).enabled()) {
        last_tick_ = std::chrono::steady_clock::now();
        (*timer_).set_interval(std::chrono::milliseconds(16));
        (*timer_).start();
    }
}

void PocketLibraryView::render() {
    if (frame_.w <= 0 || frame_.h <= 0)
        return;
    make_wood_tiles(scale_, wood_);
    static_cast<void>(printer_.load());
    if (reader_.is_open()) {
        apply_look();
        reader_.draw(frame_, scale_, printer_, wood_);
    } else {
        const int lifted = hover_ >= 0 ? hover_ : cursor_;
        // A search that found nothing says so on the bare wall.
        const std::string note = !shown_.empty() && first_on_shelf() < 0
                                     ? "No title or author has \u201c" + query_ + "\u201d"
                                     : std::string();
        draw_shelf(frame_, scale_, shelf_, scroll_, library_.shelf(), on_shelf(lifted) ? lifted : -1, wood_,
                   printer_, note);
    }
    if (about_ && reader_.is_open()) {
        const ShelfBook* book = open_shelf_book();
        if (book != nullptr)
            draw_help_card(frame_, scale_, printer_, about_paragraphs(*book));
    }
    if (help_)
        draw_help_card(frame_, scale_, printer_, help_paragraphs());
}

void PocketLibraryView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
        surface_ = gf::LiveSurface::create(description);
        gf::Window* window = attached_window();
        if (surface_ && window != nullptr)
            direct_ = (*window).queue_live_surface_presentation(shared_from_this(), surface_);
    }
    if (!surface_)
        return;
    gf::LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
    const bool matches = lease && static_cast<int>(lease.width()) == device_width_ &&
                         static_cast<int>(lease.height()) == device_height_ && frame_.w == device_width_ &&
                         frame_.h == device_height_;
    if (matches) {
        // Both are BGRA premultiplied; the surface's rows may be wider than the picture.
        const std::span<std::byte> destination = lease.pixels();
        const std::size_t row_bytes = lease.row_bytes();
        const std::size_t picture_bytes = static_cast<std::size_t>(device_width_) * 4;
        for (int y = 0; y < device_height_; ++y) {
            const std::size_t row = static_cast<std::size_t>(y);
            std::memcpy(destination.data() + row * row_bytes, frame_.px.data() + row * picture_bytes, picture_bytes);
        }
        static_cast<void>(lease.publish());
    } else {
        // The surface was busy or is being resized: draw again on the next tick.
        request_frame();
    }
    if (!direct_)
        invalidate(gf::Dirty::paint);
}

// What a screen reader is told is in front of it.
void PocketLibraryView::describe() {
    if (reader_.is_open()) {
        std::string book;
        for (const ShelfBook& shelved : library_.shelf())
            if (shelved.id == reader_.place().book)
                book = shelved.title;
        set_accessible_name("Pocket Library. " + book + ". " + reader_.describe() +
                            ". Arrow keys turn the pages; C shows the contents; Escape returns to the shelf.");
    } else {
        std::size_t kept = 0;
        for (std::size_t n = 0; n < library_.shelf().size(); ++n)
            kept += on_shelf(static_cast<int>(n)) ? 1 : 0;
        const std::string found = query_.empty() || shown_.empty()
                                      ? "A shelf of " + std::to_string(kept) + " books"
                                      : std::to_string(kept) + " books found for " + query_;
        set_accessible_name("Pocket Library. " + found +
                            ". Arrow keys choose a book; Enter opens it; Command or Control F searches by title or author.");
    }
}

// ---------------------------------------------------------------- reading

void PocketLibraryView::open_book(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= library_.shelf().size())
        return;
    const ShelfBook& book = library_.shelf()[static_cast<std::size_t>(index)];
    finish_preparing();
    const Work* work = library_.work(book.id);
    if (work == nullptr)
        return;
    LayoutOptions options;
    options.step = step_;
    options.typeface = static_cast<Typeface>(marks_.face);
    if (book.id == "concordance") {
        options.bible = library_.work("bsb");
        options.places = library_.bible_places();
    }
    reader_.open(*work, options, place_in(marks_, book.id));
    marks_.open = book.id;
    hover_ = -1;
    cursor_ = index;
    place_search();
    activate();
    describe();
    persist();
    request_frame();
}

void PocketLibraryView::close_book() {
    if (!reader_.is_open())
        return;
    set_place(marks_, reader_.place());
    about_ = false;
    reader_.close();
    marks_.open.clear();
    place_search();
    if (on_shelf(cursor_))
        scroll_ = scroll_to_show(shelf_, cursor_, scroll_);
    activate();
    describe();
    persist();
    request_frame();
}

// A page turning: the leaf crosses right to left going forward, left to right going
// back. Each turn takes the next of ten variations (audio_src/make_page.py), seven on
// each time so neighbours differ, a shade higher or lower, so reading quickly does not
// repeat one sound.
void PocketLibraryView::rustle(bool forward) {
    static constexpr float rates[] = {1.0F, .97F, 1.02F, .98F, 1.03F};
    const unsigned variation = (turns_ * 7U) % 10U;
    const float rate = rates[turns_ % (sizeof(rates) / sizeof(rates[0]))];
    ++turns_;
    const std::string name = (forward ? "pl_page_" : "pl_page_back_") + std::to_string(variation);
    audio_sfx(name, 1.F, rate, front_ && visible());
    wake_audio();
}

void PocketLibraryView::turn(bool forward) {
    if (!reader_.is_open())
        return;
    const bool turned = forward ? reader_.next(printer_) : reader_.previous(printer_);
    if (!turned)
        return;
    rustle(forward);
    set_place(marks_, reader_.place());
    describe();
    persist();
    request_frame();
}

void PocketLibraryView::to_contents(bool shown) {
    if (!reader_.is_open() || reader_.contents_shown() == shown)
        return;
    reader_.show_contents(shown);
    describe();
    request_frame();
}

void PocketLibraryView::move_cursor(int step) {
    const int count = static_cast<int>(library_.shelf().size());
    if (!on_shelf(cursor_)) {
        cursor_ = first_on_shelf();
    } else {
        // Past the books a search left off the shelf.
        int next = cursor_ + step;
        while (next >= 0 && next < count && !on_shelf(next))
            next += step;
        if (next >= 0 && next < count)
            cursor_ = next;
    }
    if (cursor_ < 0)
        return;
    scroll_ = scroll_to_show(shelf_, cursor_, scroll_);
    request_frame();
}

void PocketLibraryView::move_plank(int planks) {
    cursor_ = on_shelf(cursor_) ? spine_beside(shelf_, cursor_, planks) : first_on_shelf();
    if (cursor_ < 0)
        return;
    scroll_ = scroll_to_show(shelf_, cursor_, scroll_);
    request_frame();
}

bool PocketLibraryView::on_shelf(int book) const {
    if (book < 0 || static_cast<std::size_t>(book) >= library_.shelf().size())
        return false;
    return shown_.empty() || shown_[static_cast<std::size_t>(book)];
}

int PocketLibraryView::first_on_shelf() const {
    for (std::size_t n = 0; n < library_.shelf().size(); ++n)
        if (on_shelf(static_cast<int>(n)))
            return static_cast<int>(n);
    return -1;
}

// ---------------------------------------------------------------- the search

void PocketLibraryView::lay_out_bookcase() {
    shelf_ = lay_out_shelf(width_, height_, library_.shelf(), shown_);
    scroll_ = std::clamp(scroll_, 0.0, scroll_limit(shelf_));
}

// The field shows with the bookcase, right of the capsule and, in the shell, left of
// its help mark at the top right.
void PocketLibraryView::place_search() {
    if (!search_)
        return;
    const bool wanted = !reader_.is_open() && !help_;
    if ((*search_).visible() != wanted)
        (*search_).set_visible(wanted);
    const double right = width_ - (options_.hosted ? 56 : 16);
    const double wide = std::clamp(right - 300, 140.0, 240.0);
    set_child_layout(search_, {right - wide, 9, wide, 28});
}

bool PocketLibraryView::search_focused() const {
    const gf::Window* window = attached_window();
    return search_ && window != nullptr && (*window).focused_control() == search_;
}

void PocketLibraryView::focus_search() {
    gf::Window* window = attached_window();
    if (!search_ || window == nullptr || reader_.is_open())
        return;
    (*search_).select_all();
    static_cast<void>((*window).request_focus(search_));
}

// Each letter typed keeps only the books whose title or author has every word sought,
// standing in shelf order on as few planks as they need; the first is lifted.
void PocketLibraryView::searched(const std::string& text) {
    if (text == query_)
        return;
    query_ = text;
    shown_.clear();
    bool every = true;
    for (const ShelfBook& book : library_.shelf()) {
        const bool kept = book_matches(book, query_);
        shown_.push_back(kept);
        every = every && kept;
    }
    if (every)
        shown_.clear();
    lay_out_bookcase();
    hover_ = -1;
    if (!shown_.empty()) {
        cursor_ = first_on_shelf();
        scroll_ = 0;
    } else if (on_shelf(cursor_)) {
        scroll_ = scroll_to_show(shelf_, cursor_, scroll_);
    }
    describe();
    request_frame();
}

// Enter opens the book lifted: the first found.
void PocketLibraryView::search_committed(const std::string&) {
    if (on_shelf(cursor_) && !shown_.empty())
        open_book(cursor_);
    else
        activate();
}

// Escape empties the field; the whole library stands again.
void PocketLibraryView::search_cancelled() {
    if (search_ && !(*search_).text().empty())
        (*search_).set_text(std::string());
    searched(std::string());
    activate();
}

void PocketLibraryView::scroll_shelf(double points) {
    const double next = std::clamp(scroll_ + points, 0.0, scroll_limit(shelf_));
    if (next == scroll_)
        return;
    scroll_ = next;
    hover_ = -1;
    request_frame();
}

const ShelfBook* PocketLibraryView::open_shelf_book() const {
    if (!reader_.is_open())
        return nullptr;
    const std::string id = reader_.place().book;
    for (const ShelfBook& book : library_.shelf())
        if (book.id == id)
            return &book;
    return nullptr;
}

void PocketLibraryView::set_about(bool open) {
    const bool shown = open && reader_.is_open();
    if (about_ == shown)
        return;
    about_ = shown;
    request_frame();
}

void PocketLibraryView::set_help(bool open) {
    // In the shell, help is its help book; on its own the game shows its own card.
    if (open && games::route_help(*this))
        return;
    if (help_ == open)
        return;
    help_ = open;
    place_search();
    request_frame();
}

// ---------------------------------------------------------------- input

void PocketLibraryView::on_pointer(gf::PointerEvent& event) {
    const gf::Point local = point_from_window(event.position);
    const int x = static_cast<int>(std::lround(local.x * scale_));
    const int y = static_cast<int>(std::lround(local.y * scale_));
    if (help_ || about_) {
        if (event.action == gf::PointerAction::down) {
            set_help(false);
            set_about(false);
            event.handled = true;
        }
        return;
    }
    if (!reader_.is_open()) {
        const int under = spine_at(shelf_, scroll_, local.x, local.y);
        if (event.action == gf::PointerAction::wheel) {
            scroll_shelf(-event.wheel_delta.y);
            event.handled = true;
            return;
        }
        if (event.action == gf::PointerAction::move || event.action == gf::PointerAction::leave) {
            const int hover = event.action == gf::PointerAction::leave ? -1 : under;
            if (hover != hover_) {
                hover_ = hover;
                set_cursor(hover >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
                request_frame();
            }
            return;
        }
        if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
            activate();
            if (under >= 0)
                open_book(under);
            event.handled = true;
        }
        return;
    }
    if (event.action == gf::PointerAction::wheel) {
        const double delta = event.wheel_delta.y != 0 ? event.wheel_delta.y : -event.wheel_delta.x;
        if (delta != 0)
            turn(delta < 0);
        event.handled = true;
        return;
    }
    if (event.action == gf::PointerAction::move) {
        const bool over = reader_.contents_shown() ? reader_.contents_entry_at(x, y) >= 0 : reader_.side_at(x, y) != 0;
        set_cursor(over ? gf::CursorKind::hand : gf::CursorKind::arrow);
        return;
    }
    if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
        activate();
        if (reader_.contents_shown()) {
            const int section = reader_.contents_entry_at(x, y);
            if (section >= 0) {
                reader_.to_section(static_cast<std::size_t>(section));
                rustle(true);
                set_place(marks_, reader_.place());
                describe();
                persist();
                request_frame();
            }
        } else {
            const int side = reader_.side_at(x, y);
            if (side != 0)
                turn(side > 0);
        }
        event.handled = true;
    }
}

void PocketLibraryView::on_key(gf::KeyEvent& event) {
    if (event.handled || event.action != gf::KeyAction::down)
        return;
    using Key = gf::PhysicalKey;
    const std::uint32_t key = event.physical_key;
    if (search_focused()) {
        // The field keeps its keys; Down or Tab goes on to the books found.
        if (key == Key::down || key == Key::tab) {
            activate();
            if (!on_shelf(cursor_))
                move_cursor(1);
            event.handled = true;
        }
        return;
    }
    event.handled = true;
    if (help_) {
        if (key == Key::escape || key == Key::f1 || key == Key::enter || key == Key::space)
            set_help(false);
        return;
    }
    if (about_) {
        if (key == Key::escape || key == Key::i || key == Key::enter || key == Key::space)
            set_about(false);
        return;
    }
    if (key == Key::f1) {
        set_help(true);
        return;
    }
    if (!reader_.is_open()) {
        const bool command = gf::has_modifier(event.modifiers, gf::Modifier::control) ||
                             gf::has_modifier(event.modifiers, gf::Modifier::meta);
        if (key == Key::f && command)
            focus_search();
        else if (key == Key::escape && !query_.empty())
            search_cancelled();
        else if (key == Key::left)
            move_cursor(-1);
        else if (key == Key::right)
            move_cursor(1);
        else if (key == Key::up)
            move_plank(-1);
        else if (key == Key::down)
            move_plank(1);
        else if (key == Key::page_up)
            scroll_shelf(-shelf_.height * 0.8);
        else if (key == Key::page_down)
            scroll_shelf(shelf_.height * 0.8);
        else if ((key == Key::enter || key == Key::space) && on_shelf(cursor_))
            open_book(cursor_);
        else
            event.handled = false;
        return;
    }
    if (key == Key::right || key == Key::page_down || key == Key::space || key == Key::down)
        turn(true);
    else if (key == Key::left || key == Key::page_up || key == Key::up || key == Key::backspace)
        turn(false);
    else if (key == Key::c)
        to_contents(!reader_.contents_shown());
    else if (key == Key::i)
        set_about(true);
    else if (key == Key::escape) {
        if (reader_.contents_shown())
            to_contents(false);
        else
            close_book();
    } else
        event.handled = false;
}

// ---------------------------------------------------------------- the capsule

std::vector<games::GameCommand> PocketLibraryView::commands() const {
    // id, label, enabled, checked, primary. Primary commands show while the capsule is folded.
    std::vector<games::GameCommand> list;
    if (reader_.is_open()) {
        list.push_back({"shelf", "Library", true, false, true});
        list.push_back({"contents", "Contents", true, reader_.contents_shown(), true});
        list.push_back({"previous", "Previous page", true, false, false});
        list.push_back({"next", "Next page", true, false, false});
        list.push_back({"about", "About this book", true, about_, false});
    } else {
        const int chosen = hover_ >= 0 ? hover_ : cursor_;
        std::string label = "Open book";
        if (on_shelf(chosen))
            label = "Open " + library_.shelf()[static_cast<std::size_t>(chosen)].title;
        list.push_back({"open", label, on_shelf(chosen), false, true});
        list.push_back({"search", "Search", true, search_focused(), false});
        // One command for each plank's category: it brings that shelf into view.
        for (const PlankBox& plank : shelf_.planks)
            if (!plank.label.empty() && !plank.share_alike)
                list.push_back({"go:" + plank.label, plank.label, true, false, false});
    }
    list.push_back({"help", "Help", true, help_, false});
    return list;
}

void PocketLibraryView::run_command(std::string_view id) {
    if (id == "open") {
        const int chosen = hover_ >= 0 ? hover_ : cursor_;
        if (on_shelf(chosen))
            open_book(chosen);
        return;
    }
    if (id == "search") {
        focus_search();
        return;
    }
    if (id.substr(0, 3) == "go:") {
        const std::string_view category = id.substr(3);
        for (std::size_t n = 0; n < library_.shelf().size(); ++n) {
            if (library_.shelf()[n].category == category && on_shelf(static_cast<int>(n))) {
                cursor_ = static_cast<int>(n);
                scroll_ = std::clamp(shelf_.planks[shelf_.spines[n].plank].top - shelf_.spines[n].height - 40, 0.0,
                                     scroll_limit(shelf_));
                request_frame();
                return;
            }
        }
        return;
    }
    if (id == "shelf")
        close_book();
    else if (id == "contents")
        to_contents(!reader_.contents_shown());
    else if (id == "previous")
        turn(false);
    else if (id == "next")
        turn(true);
    else if (id == "help")
        set_help(!help_);
    else if (id == "about")
        set_about(!about_);
}

// ---------------------------------------------------------------- the Settings screen

std::vector<games::GameSetting> PocketLibraryView::settings() const {
    using Kind = games::GameSetting::Kind;
    games::GameSetting size{"type", "Type size", Kind::choice, static_cast<double>(step_)};
    for (const char* name : step_names)
        size.choices.push_back(name);
    size.note = "Every book is set again at the size chosen.";
    const games::GameSetting face{"face", "Typeface", Kind::choice, static_cast<double>(marks_.face),
                                  {"As each book is set", "Literata", "EB Garamond"}, 0, 1, 1,
                                  "As set, the Bible is in EB Garamond and every other book in Literata."};
    games::GameSetting paper{"paper", "Paper", Kind::choice,
                             static_cast<double>(std::clamp(marks_.paper, 0, paper_count - 1))};
    for (const char* name : paper_names)
        paper.choices.push_back(name);
    paper.note = "Night is light ink on dark paper; Phosphor, green on black.";
    const games::GameSetting texture{"texture", "Paper texture", Kind::slider, marks_.texture / 100.0, {}, 0, 1, .05,
                                     "Halfway is the paper as it is made; none is a flat sheet."};
    const games::GameSetting gutter{"gutter", "Shadow in the gutter", Kind::slider, marks_.gutter / 100.0, {}, 0, 1,
                                    .05, ""};
    return {size, face, paper, texture, gutter};
}

void PocketLibraryView::change_setting(std::string_view id, double value) {
    const int chosen = static_cast<int>(std::lround(value));
    const int percent = static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 100));
    if (id == "type" && chosen >= 0 && chosen <= 2 && chosen != static_cast<int>(step_)) {
        step_ = static_cast<page::TypeStep>(chosen);
        marks_.step = chosen;
    } else if (id == "face" && chosen >= 0 && chosen <= 2 && chosen != marks_.face) {
        marks_.face = chosen;
    } else if (id == "paper" && chosen >= 0 && chosen < paper_count && chosen != marks_.paper) {
        marks_.paper = chosen;
    } else if (id == "texture" && percent != marks_.texture) {
        marks_.texture = percent;
    } else if (id == "gutter" && percent != marks_.gutter) {
        marks_.gutter = percent;
    } else {
        return;
    }
    persist();
    request_frame();
}

// The reader's choices, given to the open book before it is drawn.
void PocketLibraryView::apply_look() {
    reader_.set_step(step_);
    reader_.set_typeface(static_cast<Typeface>(marks_.face));
    PageLook look;
    look.paper = marks_.paper;
    look.texture = marks_.texture / 100.0;
    look.gutter = marks_.gutter / 100.0;
    reader_.set_look(look);
}

bool PocketLibraryView::back() {
    if (!reader_.is_open())
        return false;
    close_book();
    return true;
}

// ---------------------------------------------------------------- the bookmarks

std::string_view PocketLibraryView::save_name() const {
    return options_.dev ? "pocket_library-dev-v1.txt" : "pocket_library-v1.txt";
}

void PocketLibraryView::persist() {
    if (reader_.is_open())
        set_place(marks_, reader_.place());
    static_cast<void>(games::save_game_data(game_id, save_name(), encode_bookmarks(marks_)));
}

}  // namespace pl
