#include "planet_sandbox/project_history.h"
#include "planet_sandbox/project_recovery.h"
#include "planet_sandbox/studio_ui.h"
#include "planet_sandbox/solar_system.h"
#include "planet_sandbox/sculpt.h"
#ifdef NDEBUG
#error Document tests require enabled assertions
#endif
#include <cassert>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace planet_sandbox;

static void test_history_groups_gestures_and_restores_saved_content() {
    auto project = make_solar_system_project();
    ProjectHistory history(project);
    assert(!history.dirty() && !history.can_undo() && !history.can_redo());
    project.bodies[3].design.relief = 1.5;
    history.record(project, "Terrain relief", "earth", "earth:relief");
    project.bodies[3].design.relief = 2.5;
    history.record(project, "Terrain relief", "earth", "earth:relief");
    history.finish();
    assert(history.undo_count() == 1 && history.dirty());
    const auto undo = history.undo();
    assert(undo && undo->body_id == "earth" && undo->project.bodies[3].design.relief == 1.0);
    assert(!history.dirty() && history.can_redo());
    const auto redo = history.redo();
    assert(redo && redo->project.bodies[3].design.relief == 2.5);
    history.mark_saved();
    assert(!history.dirty());
    history.undo();
    assert(history.dirty());
    history.redo();
    assert(!history.dirty());
}

static void test_history_preserves_order_branches_and_noops() {
    auto project = make_solar_system_project();
    ProjectHistory history(project);
    project.bodies[3].design.cloud_opacity = 0.7;
    history.record(project, "Cloud cover", "earth");
    auto mars = project;
    mars.bodies[4].design.relief = 2.0;
    history.record(mars, "Terrain relief", "mars");
    assert(history.undo()->body_id == "mars");
    assert(!history.record(project, "No change", "earth"));
    assert(history.can_redo());
    apply_sculpt_brush(project.bodies[3].design, BodyKind::Earth, SolarShapeLayer::Visible,
                       SculptMode::Raise, 0.3, 0.4, 0.3, 0.7);
    history.record(project, "Sculpt", "earth", "stroke");
    history.finish();
    assert(!history.can_redo());
    auto reset = project;
    reset.bodies[3].design = baseline_design(BodyKind::Earth);
    history.record(reset, "Reset body", "earth");
    auto undo = history.undo();
    assert(!undo->project.bodies[3].design.sculpt_visible.empty());
    undo = history.undo();
    assert(undo->project.bodies[3].design.sculpt_visible.empty());
    assert(undo->project.bodies[3].design.cloud_opacity == 0.7);
    undo = history.undo();
    assert(undo && !history.dirty() && !history.can_undo());
    history.reset_saved(mars);
    assert(!history.can_undo() && !history.can_redo() && !history.dirty());
}

static void test_history_is_bounded_and_separate_gestures_stay_separate() {
    auto project = make_solar_system_project();
    ProjectHistory history(project, 3, 1024 * 1024);
    for (int i = 1; i <= 5; ++i) {
        project.bodies[3].design.relief = 1.0 + i * 0.1;
        history.record(project, "Relief", "earth", "relief");
        history.finish();
    }
    assert(history.undo_count() == 3);
    for (int i = 0; i < 3; ++i) assert(history.undo());
    assert(!history.undo());
    ProjectHistory small(project, 48, 1);
    project.bodies[3].design.relief = 2.0;
    small.record(project, "Relief", "earth");
    assert(small.dirty() && small.memory_bytes() <= 1 && !small.can_undo());
    ProjectHistory turnback(project);
    project.bodies[3].design.cloud_opacity = 0.8;
    turnback.record(project, "Clouds", "earth");
    const double original_relief = project.bodies[3].design.relief;
    project.bodies[3].design.relief = 1.5;
    turnback.record(project, "Relief", "earth", "drag");
    project.bodies[3].design.relief = original_relief;
    turnback.record(project, "Relief", "earth", "drag");
    project.bodies[3].design.relief = 3.0;
    turnback.record(project, "Relief", "earth", "drag");
    turnback.finish();
    assert(turnback.undo_count() == 2);
    assert(turnback.undo()->project.bodies[3].design.cloud_opacity == 0.8);
}

static void test_atomic_save_and_recovery_preserve_source_and_validate_drafts() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("planet-doc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const auto path = dir / std::filesystem::u8path(u8"行星.json");
    auto project = make_solar_system_project();
    const auto saved = project_document(project);
    atomic_write_document(path, saved);
    project.bodies[3].design.relief = 2.0;
    const auto draft = project_document(project);
    RecoveryWriter writer;
    writer.submit(path, saved, draft);
    writer.flush();
    assert(writer.error().empty());
    auto offer = read_recovery(path);
    assert(offer && offer->document == draft && offer->base_document == saved && !offer->source_changed);
    std::ifstream source(path);
    assert(project_document(read_project(source)) == saved);
    project.bodies[4].design.relief = 2.0;
    atomic_write_document(path, project_document(project));
    offer = read_recovery(path);
    assert(offer && offer->source_changed);
    writer.submit(path, saved, project_document(project));
    writer.clear(path);
    assert(!read_recovery(path));
    writer.submit(path, saved, draft);
    writer.submit(path, saved, saved);
    writer.flush();
    assert(read_recovery(path)->document == saved);
    {
        std::ofstream corrupt(recovery_path(path));
        corrupt << "broken draft";
    }
    bool rejected = false;
    try { read_recovery(path); } catch (const std::exception&) { rejected = true; }
    assert(rejected && std::filesystem::exists(recovery_path(path)));
    bool failed = false;
    try { atomic_write_document(path / "not-a-directory.json", draft); }
    catch (const std::exception&) { failed = true; }
    assert(failed && std::filesystem::exists(path));
    writer.clear(path);
    std::filesystem::remove_all(dir);
}

static void test_document_edits_save_failures_and_recovery_restart() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("planet-studio-doc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const auto path = dir / "project.json";
    const auto initial = make_solar_system_project();
    atomic_write_document(path, project_document(initial));
    {
        StudioState state(initial, path.string());
        initialize_studio_document(state);
        auto edited = initial;
        edited.bodies[3].design.relief = 2.5;
        assert(apply_studio_edit(state, edited, "Relief", "earth", "drag"));
        edited.bodies[3].design.relief = 3.0;
        assert(apply_studio_edit(state, edited, "Relief", "earth", "drag"));
        state.history.finish();
        assert(state.history.dirty() && state.history.undo_count() == 1);
        assert(undo_studio_edit(state));
        assert(state.selected == 3 && !state.history.dirty());
        assert(redo_studio_edit(state) && state.history.dirty());
        std::string bad = (path / "invalid.json").string();
        std::copy(bad.begin(), bad.end(), state.path.begin());
        state.path[bad.size()] = '\0';
        assert(!save_studio_document(state) && state.history.dirty());
        shutdown_studio_document(state);
    }
    {
        StudioState state(initial, path.string());
        initialize_studio_document(state);
        assert(state.recovery_offer && !state.recovery_offer->source_changed);
        assert(restore_studio_draft(state));
        assert(state.session.project().bodies[3].design.relief == 3.0 && state.history.dirty());
        assert(save_studio_document(state) && !state.history.dirty());
        assert(!read_recovery(path));
        assert(!state.history.can_redo());
    }
    std::filesystem::remove_all(dir);
}

static void test_corrupt_draft_is_preserved_until_explicit_discard() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("planet-corrupt-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const auto path = dir / "project.json";
    auto initial = make_solar_system_project();
    atomic_write_document(path, project_document(initial));
    atomic_write_document(recovery_path(path), "corrupt draft to preserve");
    {
        StudioState state(initial, path.string());
        initialize_studio_document(state);
        assert(!state.recovery_error.empty() && !state.recovery_offer);
        initial.bodies[3].design.relief = 2.0;
        apply_studio_edit(state, initial, "Relief", "earth");
        update_studio_document(state, 2.0);
        shutdown_studio_document(state);
        std::ifstream input(recovery_path(path));
        std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        assert(text == "corrupt draft to preserve");
        input.close();
        assert(save_studio_document(state));
        std::ifstream after_save(recovery_path(path));
        const std::string preserved{std::istreambuf_iterator<char>(after_save), std::istreambuf_iterator<char>()};
        assert(preserved == "corrupt draft to preserve");
        after_save.close();
        initial.bodies[3].design.relief = 2.5;
        apply_studio_edit(state, initial, "Relief", "earth");
        discard_studio_draft(state);
        update_studio_document(state, 2.0);
        shutdown_studio_document(state);
        assert(read_recovery(path)->document == project_document(initial));
    }
    std::filesystem::remove_all(dir);
}

static std::filesystem::path review_directory(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() /
        (std::string("planet-review-") + name + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    return dir;
}

static void test_switch_preserves_corrupt_outgoing_draft() {
    const auto dir = review_directory("switch");
    const auto a = dir / "a.json", b = dir / "b.json";
    auto project = make_solar_system_project();
    atomic_write_document(a, project_document(project));
    atomic_write_document(b, project_document(project));
    atomic_write_document(recovery_path(a), "keep corrupt original");
    {
        StudioState state(project, a.string());
        initialize_studio_document(state);
        project.bodies[3].design.relief = 2.0;
        apply_studio_edit(state, project, "Relief", "earth");
        assert(load_studio_document(state, b, true));
        std::ifstream input(recovery_path(a));
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        assert(text == "keep corrupt original");
    }
    std::filesystem::remove_all(dir);
}

static void test_undo_restoration_clears_rejected_draft() {
    const auto dir = review_directory("restore-undo");
    const auto path = dir / "project.json";
    auto project = make_solar_system_project();
    const auto saved = project_document(project);
    atomic_write_document(path, saved);
    project.bodies[3].design.relief = 2.0;
    {
        RecoveryWriter writer;
        writer.submit(path, saved, project_document(project));
        writer.flush();
    }
    {
        StudioState state(make_solar_system_project(), path.string());
        initialize_studio_document(state);
        assert(restore_studio_draft(state));
        assert(undo_studio_edit(state) && !state.history.dirty());
        shutdown_studio_document(state);
        assert(!std::filesystem::exists(recovery_path(path)));
    }
    std::filesystem::remove_all(dir);
}

static void test_recovery_retries_after_temporary_directory_failure() {
    const auto dir = review_directory("retry");
    const auto folder = dir / "missing";
    const auto path = folder / "project.json";
    auto project = make_solar_system_project();
    {
        StudioState state(project, path.string());
        initialize_studio_document(state);
        project.bodies[3].design.relief = 2.0;
        apply_studio_edit(state, project, "Relief", "earth");
        update_studio_document(state, 2.0);
        state.recovery_writer->flush();
        assert(!state.recovery_writer->error().empty());
        update_studio_document(state, 0.0);
        assert(!state.recovery_error.empty());
        std::filesystem::create_directories(folder);
        update_studio_document(state, 6.0);
        state.recovery_writer->flush();
        update_studio_document(state, 0.0);
        assert(read_recovery(path));
        assert(state.recovery_error.empty());
    }
    std::filesystem::remove_all(dir);
}

static void test_startup_can_restore_when_source_is_invalid_or_missing() {
    const auto dir = review_directory("startup");
    const auto path = dir / "project.json";
    auto project = make_solar_system_project();
    const auto saved = project_document(project);
    atomic_write_document(path, saved);
    project.bodies[3].design.relief = 2.0;
    {
        RecoveryWriter writer;
        writer.submit(path, saved, project_document(project));
        writer.flush();
    }
    atomic_write_document(path, "corrupt source preserved until explicit save");
    {
        StudioState state(open_studio_project(path, false), path.string());
        initialize_studio_document(state);
        assert(state.recovery_offer && state.recovery_offer->source_changed);
        assert(restore_studio_draft(state) && state.history.dirty());
        std::ifstream input(path);
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        assert(text == "corrupt source preserved until explicit save");
        shutdown_studio_document(state);
    }
    std::filesystem::remove(path);
    {
        StudioState state(open_studio_project(path, false), path.string());
        initialize_studio_document(state);
        assert(state.recovery_offer && restore_studio_draft(state));
        assert(state.history.dirty());
        assert(save_studio_document(state));
        assert(std::filesystem::exists(path) && !read_recovery(path));
    }
    std::filesystem::remove_all(dir);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string test = argv[1];
        if (test == "switch") test_switch_preserves_corrupt_outgoing_draft();
        else if (test == "restore") test_undo_restoration_clears_rejected_draft();
        else if (test == "retry") test_recovery_retries_after_temporary_directory_failure();
        else return 2;
        return 0;
    }
    test_switch_preserves_corrupt_outgoing_draft();
    test_undo_restoration_clears_rejected_draft();
    test_recovery_retries_after_temporary_directory_failure();
    test_startup_can_restore_when_source_is_invalid_or_missing();
    test_history_groups_gestures_and_restores_saved_content();
    test_history_preserves_order_branches_and_noops();
    test_history_is_bounded_and_separate_gestures_stay_separate();
    test_atomic_save_and_recovery_preserve_source_and_validate_drafts();
    test_document_edits_save_failures_and_recovery_restart();
    test_corrupt_draft_is_preserved_until_explicit_discard();
}
