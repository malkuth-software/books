// Sets the same page of prose in each candidate reading face, on the library's India paper,
// through the library's own typesetter and rasterizer, and lays the pages side by side
// in one picture for the owner to choose from. Not shipped.
//
//   face_specimen <avot.tsv> <fonts dir> <out.ppm> <page height px>
//
// The fonts directory holds <Name>.ttf and <Name>-Italic.ttf for every candidate.
#include "../src/book.hpp"
#include "../src/page/font_set.hpp"
#include "../src/page/page_layout.hpp"
#include "../src/page/page_raster.hpp"
#include "../src/page/text_layout.hpp"
#include "paper.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace page = pl::page;

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return bytes;
}

struct Candidate {
    const char* file;
    const char* label;
};

// One paragraph of the sample: its text with italic spans.
struct Sample {
    std::string text;
    std::vector<bool> italic;  // per byte
    bool heading = false;
};

std::vector<Sample> sample_of(const pl::Work& work) {
    std::vector<Sample> samples;
    for (const pl::Division& division : work.divisions) {
        for (const pl::Chapter& chapter : division.chapters) {
            if (chapter.label != "I")
                continue;
            Sample title;
            title.text = "CHAPTER I";
            title.italic.assign(title.text.size(), false);
            title.heading = true;
            samples.push_back(title);
            for (const pl::Block& block : chapter.blocks) {
                if (block.style != pl::BlockStyle::paragraph || samples.size() > 9)
                    continue;
                Sample made;
                for (const pl::Inline& item : block.inlines) {
                    if (item.kind != pl::InlineKind::text)
                        continue;
                    for (const char byte : item.text) {
                        if (byte == ' ' && !made.text.empty() && made.text.back() == ' ')
                            continue;
                        made.text.push_back(byte);
                        made.italic.push_back((item.flags & pl::span_italic) != 0);
                    }
                }
                samples.push_back(made);
            }
        }
    }
    return samples;
}

void place_line(const page::LineBox& line, page::LayoutUnit top, page::PageLayout& out) {
    for (const page::PlacedRun& source : line.runs) {
        page::PlacedRun run = source;
        run.x += page::content_left_lu;
        run.baseline += top;
        out.runs.push_back(run);
    }
}

page::PageLayout specimen_page(const std::vector<Sample>& samples, const char* label, page::Typesetter& setter) {
    const page::FontSet& fonts = setter.fonts();
    page::TextStyle body;
    body.face = page::FaceId::eb_garamond_regular;
    body.em = page::medium_em_lu;
    page::TextStyle italic = body;
    italic.face = page::FaceId::eb_garamond_italic;
    page::TextStyle heading = body;
    heading.em = page::medium_em_lu * 9 / 10;
    heading.tracking = page::medium_em_lu / 8;
    heading.ink = page::Ink::soft;
    page::TextStyle name = body;
    name.em = page::medium_em_lu * 15 / 10;

    page::PageLayout out;
    page::LayoutUnit top = page::content_top_lu;
    const page::LayoutUnit bottom = page::content_top_lu + page::content_height_lu;
    std::vector<Sample> all;
    Sample title;
    title.text = label;
    title.italic.assign(title.text.size(), false);
    all.push_back(title);
    all.insert(all.end(), samples.begin(), samples.end());
    for (std::size_t n = 0; n < all.size() && top < bottom; ++n) {
        const Sample& sample = all[n];
        page::ParagraphInput input;
        input.text = sample.text;
        const page::TextStyle& base = n == 0 ? name : (sample.heading ? heading : body);
        input.styles = {base, italic};
        for (std::size_t i = 0; i < sample.text.size(); ++i) {
            input.style_of_byte.push_back(sample.italic[i] && n > 0 && !sample.heading ? 1 : 0);
            input.word_of_byte.push_back(page::no_word);
        }
        const page::FaceMetrics metrics = fonts.metrics(base.face, base.em);
        page::ParagraphFrame frame;
        frame.line_height = base.em * 135 / 100;
        frame.baseline = (frame.line_height - metrics.ascender - metrics.descender) / 2 + metrics.ascender;
        frame.alignment = n <= 1 || sample.heading ? page::Alignment::centered : page::Alignment::justified;
        frame.first_indent = n > 2 ? body.em : 0;
        std::vector<page::LineBox> lines;
        setter.set_paragraph(input, frame, lines);
        if (n == 0)
            top += body.em / 2;
        for (const page::LineBox& line : lines) {
            if (top + line.height > bottom)
                break;
            place_line(line, top, out);
            top += line.height;
        }
        if (n <= 1 || sample.heading)
            top += body.em * 6 / 10;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "face_specimen <avot.tsv> <fonts dir> <out.ppm> <page height px>\n");
        return 2;
    }
    const std::vector<std::uint8_t> records = read_file(argv[1]);
    pl::Work work;
    if (!pl::read_work("avot", std::string_view(reinterpret_cast<const char*>(records.data()), records.size()), work))
        return 1;
    const std::vector<Sample> samples = sample_of(work);
    const std::string fonts_dir = argv[2];
    const std::uint32_t page_height = static_cast<std::uint32_t>(std::stoul(argv[4]));
    const std::uint32_t page_width = page_height * 715U / 1000U;
    const Candidate candidates[] = {
        {"Literata", "A · Literata"},          {"CharisSIL", "B · Charis SIL"},
        {"SourceSerif4", "C · Source Serif 4"}, {"CrimsonPro", "D · Crimson Pro"},
        {"Newsreader", "E · Newsreader"},       {"GentiumBookPlus", "F · Gentium Book Plus"},
        {"EBGaramond", "G · EB Garamond (the Bible's)"},
    };
    const std::uint32_t columns = 4, rows = 2, gap = page_height / 40;
    const std::uint32_t width = columns * page_width + (columns + 1) * gap;
    const std::uint32_t height = rows * page_height + (rows + 1) * gap;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 0x2e;
        pixels[i + 1] = 0x2b;
        pixels[i + 2] = 0x2b;
        pixels[i + 3] = 255;
    }
    const page::PixelTarget target{pixels.data(), static_cast<std::size_t>(width) * 4, width, height};

    // The Pocket Bible's paper (interview P001 India, P003 a soft gutter shadow, P012
    // quiet): the paper engine's India preset, #f0ece0, as the interview mockup had it.
    paper::Parameters stock = paper::preset(paper::Preset::india);
    stock.pixels_per_point = 2.0;
    stock.seed = 1909;
    paper::Shading shading;
    shading.gutter = paper::Gutter::left;
    shading.edge_width = 10;
    paper::Image sheet;
    if (!paper::generate_sheet(stock, static_cast<int>(page_width), static_cast<int>(page_height), shading, sheet))
        return 1;
    std::vector<std::uint8_t> paper_bgra(sheet.rgba.size());
    for (std::size_t i = 0; i < sheet.rgba.size(); i += 4) {
        paper_bgra[i] = sheet.rgba[i + 2];
        paper_bgra[i + 1] = sheet.rgba[i + 1];
        paper_bgra[i + 2] = sheet.rgba[i];
        paper_bgra[i + 3] = 255;
    }
    const page::PaperImage paper_image{paper_bgra.data(), static_cast<std::size_t>(page_width) * 4, page_width,
                                       page_height};

    for (std::size_t n = 0; n < sizeof(candidates) / sizeof(candidates[0]); ++n) {
        const std::vector<std::uint8_t> regular = read_file(fonts_dir + "/" + candidates[n].file + "-Regular.ttf");
        const std::vector<std::uint8_t> regular_plain = read_file(fonts_dir + "/" + candidates[n].file + ".ttf");
        const std::vector<std::uint8_t> italic = read_file(fonts_dir + "/" + candidates[n].file + "-Italic.ttf");
        const std::vector<std::uint8_t>& roman = regular.empty() ? regular_plain : regular;
        page::FontSet fonts;
        if (!fonts.load(page::FaceId::eb_garamond_regular, roman) ||
            !fonts.load(page::FaceId::eb_garamond_italic, italic)) {
            std::fprintf(stderr, "%s did not load\n", candidates[n].file);
            return 1;
        }
        page::Typesetter setter(fonts);
        const page::PageLayout layout = specimen_page(samples, candidates[n].label, setter);
        page::PageRasterizer rasterizer(fonts);
        const std::uint32_t column = static_cast<std::uint32_t>(n) % columns;
        const std::uint32_t row = static_cast<std::uint32_t>(n) / columns;
        page::PageRect rect;
        rect.x = static_cast<std::int32_t>(gap + column * (page_width + gap));
        rect.y = static_cast<std::int32_t>(gap + row * (page_height + gap));
        rect.width = page_width;
        rect.height = page_height;
        rasterizer.draw_page(layout, target, rect, paper_image);
        std::printf("%-32s %zu runs\n", candidates[n].label, layout.runs.size());
    }
    std::ofstream out(argv[3], std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const char rgb[3] = {static_cast<char>(pixels[i + 2]), static_cast<char>(pixels[i + 1]),
                             static_cast<char>(pixels[i])};
        out.write(rgb, 3);
    }
    return out.good() ? 0 : 1;
}
