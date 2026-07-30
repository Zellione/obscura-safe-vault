#include "import_controller.h"

#include <algorithm>
#include <QFileDialog>
#include <QStandardPaths>
#include "vault/vault.h"
#include "ui/import_model.h"
#include "platform/paths.h"

ImportController::ImportController(QObject* parent)
    : QObject(parent)
    , queue_(std::make_unique<ui::ImportQueue>())
{
}

ImportController::~ImportController()
{
    if (session_active_.load(std::memory_order_acquire)) {
        end_session();
    }
}

void ImportController::begin_session(vault::Vault& vault)
{
    if (session_active_.load(std::memory_order_acquire)) {
        return;  // Already in session
    }

    vault_ = &vault;
    queue_->begin_session(vault);
    session_active_.store(true, std::memory_order_release);
    queue_count_.store(0, std::memory_order_release);
    cached_footer_.clear();
}

void ImportController::end_session()
{
    if (!session_active_.load(std::memory_order_acquire)) {
        return;  // Not in session
    }

    queue_->end_session();
    vault_ = nullptr;
    session_active_.store(false, std::memory_order_release);
    queue_count_.store(0, std::memory_order_release);
    cached_footer_.clear();

    emit queueChanged();
}

void ImportController::pickFiles()
{
    if (!vault_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    // Open file dialog for images/videos (multi-select)
    QStringList fileNames = QFileDialog::getOpenFileNames(
        nullptr,  // parent widget (nullptr for headless compatibility in tests)
        "Select Images or Videos",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        "Images & Video (*.jpg *.jpeg *.png *.gif *.bmp *.tga *.webp *.heic *.avif "
        "*.mp4 *.mkv *.webm *.mov *.m4v *.avi *.mpg *.mpeg *.wmv *.asf *.flv);;All files (*)"
    );

    if (fileNames.isEmpty()) {
        return;
    }

    // Normalize each path and filter out invalid ones
    QList<QString> normalized_paths;
    for (const auto& path : fileNames) {
        if (auto norm_path = platform::normalize_user_path(path.toStdString())) {
            normalized_paths.append(QString::fromStdString(norm_path->string()));
        }
    }

    if (!normalized_paths.isEmpty()) {
        enqueueFiles(normalized_paths);
    }
}

void ImportController::pickFolders()
{
    if (!vault_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    // Open folder dialog (single-select)
    QString folderPath = QFileDialog::getExistingDirectory(
        nullptr,  // parent widget (nullptr for headless compatibility in tests)
        "Select Folder to Import",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (folderPath.isEmpty()) {
        return;
    }

    // Normalize path
    if (auto norm_path = platform::normalize_user_path(folderPath.toStdString())) {
        enqueueFolder(QString::fromStdString(norm_path->string()));
    }
}

void ImportController::pickArchives()
{
    if (!vault_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    // Open file dialog for archives (multi-select, but typically one at a time)
    QStringList fileNames = QFileDialog::getOpenFileNames(
        nullptr,  // parent widget (nullptr for headless compatibility in tests)
        "Select Archive File",
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        "Archives (*.zip *.cbz *.7z *.rar *.tar *.tar.gz *.tar.xz *.cbr *.cb7 *.cbt);;All files (*)"
    );

    if (fileNames.isEmpty()) {
        return;
    }

    // For each archive, normalize and enqueue
    for (const auto& path : fileNames) {
        if (auto norm_path = platform::normalize_user_path(path.toStdString())) {
            enqueueArchive(QString::fromStdString(norm_path->string()));
        }
    }
}

void ImportController::enqueueFiles(const QList<QString>& paths)
{
    if (!vault_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    std::vector<std::filesystem::path> fs_paths;
    for (const auto& p : paths) {
        fs_paths.push_back(p.toStdString());
    }

    // Enqueue as root gallery
    queue_->enqueue_files(fs_paths, "");

    updateFromSnapshot();
}

void ImportController::enqueueArchive(const QString& path, const QString& gallery_name)
{
    if (!vault_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    auto archive_path = std::filesystem::path(path.toStdString());
    std::string name = gallery_name.isEmpty() ? path.toStdString() : gallery_name.toStdString();

    // Determine archive kind from extension (simplified; full impl would use import_common)
    auto ext = archive_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    ui::ImportTaskKind kind = ui::ImportTaskKind::Archive;
    if (ext == ".zip") kind = ui::ImportTaskKind::Zip;
    else if (ext == ".cbz") kind = ui::ImportTaskKind::Cbz;
    else if (ext == ".7z" || ext == ".rar" || ext == ".tar") kind = ui::ImportTaskKind::Archive;

    // Enqueue as root gallery
    queue_->enqueue_archive(archive_path, "", name, kind);

    updateFromSnapshot();
}

void ImportController::enqueueFolder(const QString& path, const QString& gallery_name)
{
    if (!vault_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    auto folder_path = std::filesystem::path(path.toStdString());
    std::string name = gallery_name.isEmpty() ? path.toStdString() : gallery_name.toStdString();

    queue_->enqueue_folder(folder_path, "", name);

    updateFromSnapshot();
}

void ImportController::enqueueVolumeSet(const QList<QString>& volume_paths, const QString& stem,
                                       const QString& gallery_name)
{
    if (!vault_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    std::vector<std::filesystem::path> volumes;
    for (const auto& p : volume_paths) {
        volumes.push_back(p.toStdString());
    }

    std::string name = gallery_name.isEmpty() ? stem.toStdString() : gallery_name.toStdString();

    // Determine volume style (simplified; full impl would detect from filenames)
    auto style = ui::VolumeStyle::NumericSuffix;  // Default to numeric suffix (.001, .002, etc.)

    queue_->enqueue_volume_set(volumes, style, stem.toStdString(), "", name, ui::ImportTaskKind::Archive);

    updateFromSnapshot();
}

void ImportController::cancel(uint64_t task_id)
{
    if (queue_) {
        (void)queue_->cancel(task_id);
        updateFromSnapshot();
    }
}

void ImportController::reorder(uint64_t task_id, int delta)
{
    if (queue_) {
        (void)queue_->reorder(task_id, delta);
        updateFromSnapshot();
    }
}

void ImportController::clearFinished()
{
    if (queue_) {
        queue_->clear_finished();
        updateFromSnapshot();
    }
}

void ImportController::setExclusiveOp(bool held)
{
    if (queue_) {
        queue_->set_exclusive(held);
    }
}

void ImportController::drain(double dt)
{
    if (!queue_ || !session_active_.load(std::memory_order_acquire)) {
        return;
    }

    // Pump the queue's internal drain
    queue_->drain(dt);

    // Update UI properties from snapshot
    updateFromSnapshot();

    // Check for errors
    if (queue_->lane_failed()) {
        // Emit lane failure signal
        auto summary = queue_->footer_summary();
        if (summary.find("Import failed") != std::string::npos) {
            emit laneFailure(QString::fromStdString(summary));
        }
    }
}

QString ImportController::footerSummary() const
{
    return cached_footer_;
}

void ImportController::updateFromSnapshot()
{
    auto tasks = queue_->snapshot();

    // Calculate queue count: running + queued items
    int count = static_cast<int>(
        std::count_if(tasks.begin(), tasks.end(),
                     [](const ui::ImportTaskInfo& t) {
                         return t.state == ui::ImportTaskState::Queued ||
                                t.state == ui::ImportTaskState::Running;
                     }));

    int old_count = queue_count_.load(std::memory_order_acquire);
    queue_count_.store(count, std::memory_order_release);

    // Update footer summary
    QString old_footer = cached_footer_;
    cached_footer_ = QString::fromStdString(queue_->footer_summary());

    if (old_count != count || old_footer != cached_footer_) {
        emit queueChanged();
    }
}
