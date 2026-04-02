#include "ao/journal/Journal.h"

#include <ao/pack/IOByteStream.h>

#include <algorithm>
#include <expected>
#include <filesystem>

namespace ao::journal {
JournalStatus ensureDirectoryExists(std::filesystem::path const& dirPath) {
    std::error_code errorCode;

    // Check if the path is a directory
    bool existsAndIsDir = std::filesystem::is_directory(dirPath, errorCode);
    if (errorCode) {
        return JournalStatus::Other;
    }

    if (!existsAndIsDir) {
        // Create the directory
        if (!std::filesystem::create_directories(dirPath, errorCode)) {
            return JournalStatus::CreateFailed;
        }
    } else if (!std::filesystem::is_directory(dirPath)) {
        return JournalStatus::PathExistsButNotDirectory;
    }

    return JournalStatus::Ok;
}

template <class Filter>
std::expected<std::vector<std::filesystem::path>, JournalStatus>
listFilesInDirectory(std::filesystem::path const& dirPath, Filter filter) {
    std::error_code errorCode;

    // Check if the path exists and is a directory
    bool existsAndIsDir = std::filesystem::is_directory(dirPath, errorCode);
    if (errorCode) {
        return std::unexpected(JournalStatus::Other);
    }

    if (!existsAndIsDir) {
        return std::unexpected(JournalStatus::PathExistsButNotDirectory);
    }

    // Vector to store file paths
    std::vector<std::filesystem::path> files;

    // Iterate over the directory and store file paths
    for (const auto& entry :
         std::filesystem::directory_iterator(dirPath, errorCode)) {
        if (errorCode) {
            return std::unexpected(JournalStatus::DirectoryIterationFailed);
        }

        auto const& path = entry.path();
        if (!std::filesystem::is_regular_file(entry.status()))
            continue;

        if (!filter(path))
            continue;

        files.emplace_back(path);
    }

    return files;
}

void JournalStorage::appendFrame(FrameBatch const& frameBatchStart,
                                 AppendOptions opts) {
    if (!m_writeStream) {
    }
}

std::optional<FrameBatchView> JournalStorage::nextFrame() {
    return {};
}
std::optional<FrameBatchView> JournalStorage::seekFrame(uint64_t frame) {
    return {};
}
JournalStatus JournalStorage::loadRoot(std::filesystem::path root) {
    m_rootPath = std::move(root);
    auto logPath = m_rootPath / "logs";

    auto status = ensureDirectoryExists(logPath);
    if (status != JournalStatus::Ok)
        return status;

    static std::string_view const prefix = "log_";
    static std::string_view const suffix = ".bin";

    auto logFiles = listFilesInDirectory(logPath, [](auto const& path) {
        auto filename = path.filename().string();
        if (!filename.starts_with(prefix))
            return false;
        if (!filename.ends_with(suffix))
            return false;
        auto isNumeric = std::all_of(
            filename.begin() + prefix.size(), filename.end() - suffix.size(),
            [](auto c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            });
        if (!isNumeric)
            return false;
    });

    if (!logFiles)
        return logFiles.error();
    for (auto const& logFile : logFiles.value()) {
        auto filename = logFile.filename().string();
        auto startFrame = std::stoull(std::string{
            filename.begin() + prefix.size(),
            filename.end() - suffix.size(),
        });
        m_fileFrames.emplace_back(startFrame, logFile);
    }
    std::sort(m_fileFrames.begin(), m_fileFrames.end(),
              [](auto const& l, auto const& r) { return l.first < r.first; });

    m_currentReadIndex = 0;
    m_writeFileSize = 0;

    // Force a log rotate, and seek to frame 0
    if (m_fileFrames.size() == 0) {
        rotateLogFile();
        seekFrame(0);
    } else {
    }
}

bool JournalStorage::shouldRotateCurrentFile() const {
    return m_writeFileSize >= m_settings.maxFileSize;
}

bool JournalStorage::rotateLogFile() {
    m_fileFrames.emplace_back(
        m_nextWriteFrame,
        m_rootPath / "logs" / std::format("log_{:09}.bin", m_nextWriteFrame));
    m_writeStream = std::make_unique<std::ofstream>(m_fileFrames.back().second,
                                                    std::ios::binary);

    return true;
}

size_t JournalStorage::getLastFrame() {
    if (m_fileFrames.empty())
        return 0;
    m_readStream = std::make_unique<std::ifstream>(m_fileFrames.back().second,
                                                   std::ios::binary);

    if (!m_readStream->good()) {
        m_status = JournalStatus::FailedOpenFile;
        return 0;
    }

    ao::pack::byte::IStreamReadStream read{*m_readStream};

    size_t lastFrame = 0;
    auto status = JournalStatus::Ok;
    FrameBatch batch;
    batch.frame = 0;
    while (status == JournalStatus::Ok) {
        lastFrame = batch.frame;
        batch.frame = 0;
        status = v1::FrameBatchV1::deserialize(read, batch);
    }
    if (status == JournalStatus::EndOfStream ||
        status == JournalStatus::Corruption) {
        return lastFrame;
    } else {
        // Return 0 as there was some sort of error which is _really_ bad
        return 0;
    }

}

}  // namespace ao::journal
