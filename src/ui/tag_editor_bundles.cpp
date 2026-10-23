#include <algorithm>
#include <format>
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/tag_editor.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "vault/tag_bundles.h"
#include "vault/vault.h"

namespace ui {
namespace {
std::vector<std::string> parse_members(std::string_view text)
{
    std::vector<std::string> tags;
    std::string member;
    bool escaped = false;
    for (char ch : text) {
        if (escaped) {
            member += ch;
            escaped = false;
        } else if (ch == '\\')
            escaped = true;
        else if (ch == ';') {
            if (!member.empty()) tags.push_back(std::move(member));
            member.clear();
        } else
            member += ch;
    }
    if (escaped) member += '\\';
    if (!member.empty()) tags.push_back(std::move(member));
    return tags;
}
}  // namespace

void TagEditor::open_bundle_form(bool create)
{
    bundle_form_ = true;
    bundle_confirm_ = bundle_delete_ = false;
    bundle_field_ = 0;
    bundle_original_name_.clear();
    bundle_name_.clear();
    bundle_members_.clear();
    const auto& bundles = vault::vault_settings(vault_).tag_bundles;
    if (create || bundle_selected_ >= static_cast<int>(bundles.size())) return;
    const auto& bundle = bundles[bundle_selected_];
    bundle_original_name_ = bundle.name.view();
    bundle_name_.set_text(bundle.name.view());
    std::string members;
    for (const auto& tag : bundle.tags) {
        if (!members.empty()) members += ';';
        for (char ch : tag.view()) {
            if (ch == ';' || ch == '\\') members += '\\';
            members += ch;
        }
    }
    bundle_members_.set_text(members);
}

void TagEditor::run_bundle_action(bool remove)
{
    const auto& bundles = vault::vault_settings(vault_).tag_bundles;
    if (bundle_selected_ >= static_cast<int>(bundles.size())) return;
    const std::string name(bundles[bundle_selected_].name.view());
    launch([this, name, remove] {
        const auto result = vault::apply_tag_bundle(vault_, node_paths_, name, remove, &progress_);
        refresh_tags();
        refresh_vocabulary();
        if (result != vault::VaultResult::Ok) error_ = "Bundle operation failed; no changes saved.";
    });
}

void TagEditor::preview_bundle_change()
{
    error_.clear();
    const auto name =
        bundle_original_name_.empty() ? std::string(bundle_name_.str()) : bundle_original_name_;
    const auto members = parse_members(bundle_members_.str());
    const bool deleting = bundle_delete_;
    launch([this, name, members, deleting] {
        auto settings = vault::vault_settings(vault_);
        const auto found = std::ranges::find_if(settings.tag_bundles, [&](const auto& b) {
            return vault::tag_ci_equal(b.name.view(), name);
        });
        size_t affected = 0, added = 0, removed = 0;
        if (found != settings.tag_bundles.end()) {
            found->tags.clear();
            if (!deleting)
                for (const auto& tag : members)
                    found->tags.emplace_back(tag);
            const auto walk = [&](auto&& self, const vault::IndexNode& node) -> void {
                if (std::ranges::find(node.tag_bundles, found->id) != node.tag_bundles.end()) {
                    ++affected;
                    vault::IndexNode copy;
                    copy.tag_provenance = node.tag_provenance;
                    copy.explicit_tags = node.explicit_tags;
                    copy.tag_bundles = node.tag_bundles;
                    if (!vault::rebuild_bundle_tags(copy, settings)) {
                        error_ = "Bundle would exceed the per-node tag limit.";
                        return;
                    }
                    const auto contains = [](const auto& tags, const auto& tag) {
                        return std::ranges::any_of(tags, [&](const auto& t) {
                            return vault::tag_ci_equal(t.view(), tag.view());
                        });
                    };
                    for (const auto& tag : node.tags)
                        if (!contains(copy.tags, tag)) ++removed;
                    for (const auto& tag : copy.tags)
                        if (!contains(node.tags, tag)) ++added;
                }
                for (const auto& child : node.children)
                    self(self, child);
            };
            walk(walk, *vault_.resolve_node(""));
        }
        bundle_preview_ = std::format("{}: {} applications, {} tags added, {} removed.",
                                      deleting ? "Delete" : "Save", affected, added, removed);
        bundle_confirm_ = error_.empty();
    });
}

bool TagEditor::handle_bundle_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
        if (bundle_confirm_)
            bundle_confirm_ = false;
        else if (bundle_form_)
            bundle_form_ = false;
        else
            bundle_mode_ = false;
        return true;
    }
    if (bundle_confirm_) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN) {
            const std::string name(bundle_name_.str());
            const auto members = parse_members(bundle_members_.str());
            const bool deleting = bundle_delete_;
            const auto original = bundle_original_name_;
            bundle_confirm_ = bundle_form_ = false;
            launch([this, name, members, deleting, original] {
                progress_.done.store(progress_.total.load());
                const auto r = deleting ? vault::delete_tag_bundle(vault_, name)
                                        : vault::save_tag_bundle(vault_, name, members, original);
                bundle_selected_ = 0;
                refresh_tags();
                refresh_vocabulary();
                if (r != vault::VaultResult::Ok) error_ = "Bundle save failed; no changes saved.";
            });
        }
        return true;
    }
    if (bundle_form_) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB) {
            bundle_field_ ^= 1;
            return true;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F5) {
            preview_bundle_change();
            return true;
        }
        auto& field = bundle_field_ == 0 ? bundle_name_ : bundle_members_;
        (void)handle_text_input_event(field, e);
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    const int count = static_cast<int>(vault::vault_settings(vault_).tag_bundles.size());
    switch (e.key.key) {
    case SDLK_F3:
        bundle_mode_ = false;
        break;
    case SDLK_UP:
        bundle_selected_ = std::max(0, bundle_selected_ - 1);
        break;
    case SDLK_DOWN:
        bundle_selected_ = std::max(0, std::min(count - 1, bundle_selected_ + 1));
        break;
    case SDLK_RETURN:
        run_bundle_action(false);
        break;
    case SDLK_DELETE:
        run_bundle_action(true);
        break;
    case SDLK_F8:
        open_bundle_form(true);
        break;
    case SDLK_F4:
        if (count) open_bundle_form(false);
        break;
    case SDLK_F6:
        if (count) {
            open_bundle_form(false);
            bundle_delete_ = true;
            preview_bundle_change();
        }
        break;
    default:
        break;
    }
    return true;
}

void TagEditor::render_bundles(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    using namespace gfx::theme;
    const float width = std::min(720.0f, W - 32), height = std::min(460.0f, H - 32);
    const float x = (W - width) / 2, y = (H - height) / 2, pitch = line_pitch(font.pixel_height());
    r.draw_round_rect({x, y, width, height}, RADIUS, SURFACE);
    r.draw_round_rect({x, y, width, height}, RADIUS, ACCENT, false);
    const auto line = [&](float row, std::string_view text) {
        r.draw_text(font, x + 16, y + 16 + row * pitch, fit_text(font, text, width - 32), TEXT);
    };
    line(0, bundle_form_ ? "Bundle administration" : "Meta-tag bundles");
    if (bundle_form_) {
        line(1, "Bundle name");
        const auto field = [&](float row, TextInputModel& model, TextFieldChrome& chrome,
                               bool focus) {
            const SDL_FRect box{x + 16, y + 16 + row * pitch, width - 32, pitch + 8};
            r.draw_round_rect(box, RADIUS_SMALL, focus ? SURFACE_HI : SURFACE);
            draw_inline_edit_text(r, font, box.x + 4, box.y + 4, box.w - 8, model, chrome);
        };
        field(2, bundle_name_, bundle_name_chrome_, bundle_field_ == 0);
        line(4, "Members separated by ; (escape ; or \\ with \\). Ordinary tags only.");
        field(5, bundle_members_, bundle_members_chrome_, bundle_field_ == 1);
        if (bundle_confirm_) {
            line(7, bundle_preview_);
            line(8, "Manual assignments and contributions from other bundles survive.");
            line(10, "[Enter] Confirm atomically  [Esc] Cancel");
        } else
            line(9, "[Tab] Switch field  [F5] Preview save  [Esc] Cancel");
    } else {
        line(1, "[F8] New  [F4] Edit  [F6] Delete definition  [Up/Down] Select");
        const auto& bundles = vault::vault_settings(vault_).tag_bundles;
        const int rows = std::max(1, static_cast<int>(height / pitch) - 6);
        const int first = std::max(0, bundle_selected_ - rows + 1);
        for (int i = first; i < static_cast<int>(bundles.size()) && i < first + rows; ++i) {
            line(static_cast<float>(3 + i - first),
                 std::format("{} {} ({} tags)", i == bundle_selected_ ? ">" : " ",
                             bundles[i].name.view(), bundles[i].tags.size()));
        }
        line(height / pitch - 3,
             "[Enter] Apply to selection  [Del] Remove from selection  [F3/Esc] Tags");
    }
    if (!error_.empty()) line(height / pitch - 2, error_);
}
}  // namespace ui
