#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>

#include <ao/pack/HashingStream.h>
#include <ao/schema/Serializer.h>
#include <ao/utils/Blake3Hasher.h>

namespace ao::journal {

// Only two types of entries
enum class RecordKind : uint32_t {
    FileHeader,
    FrameBatch,
    FrameBatchEnd,
    RecordKindMax,
};

struct CommandView {
    uint64_t messageId;
    std::span<std::byte> payload;
};

struct Command {
    uint64_t messageId;
    std::vector<std::byte> payload;
};

struct FrameBatchView {
    uint64_t frame;
    std::vector<CommandView> commands;
};

struct FrameBatch {
    uint64_t frame;
    std::vector<Command> commands;
};

enum class JournalStatus {
    Ok,
    IOError,
    EndOfStream,
    Corruption,

    PathExistsButNotDirectory,
    CreateFailed,
    DirectoryIterationFailed,
    FailedOpenFile,
    Other,
};

namespace v1 {

struct FrameBatchStart {
    AO_MEMBER(uint32_t, version) = 1;

    AO_MEMBER(uint64_t, frame);
    AO_MEMBER(uint64_t, payloadSize);
    AO_MEMBER(uint32_t, commandCount);
};
// TODO make this a set of 3 functions
// serialize, deserialize, skip
struct FrameBatchV1 {
    template <class Stream>
    static JournalStatus serialize(Stream& stream, FrameBatch const& batch) {
        pack::HashingStream<Stream, utils::hash::Blake3Hasher> stream{
            baseStream};
        stream.enableHashing();

        auto header = journal::v1::FrameBatchStart{};
        header.frame = batch.frame;
        header.commandCount = batch.commands.size();
        header.payloadSize = 0;
        for (auto const& p : batch.commands) {
            // Add 8 for the size of the message id
            header.payloadSize += 8;
            header.payloadSize += p.payload.size();
        }

        schema::serialize(stream, header);

        auto headerHash = stream.digest();
        schema::serialize(stream, headerHash);

        // Reset hash and enable
        stream.enableHashing();

        for (auto const& p : batch.commands) {
            schema::serialize(stream, p.messageId);
            uint64_t size = p.payload.size();
            schema::serialize(stream, size);
            stream.bytes({p.payload.data(), p.payload.size()},
                         p.payload.size());
        }

        auto payloadHash = stream.digest();
        stream.disableHashing();
        schema::serialize(stream, payloadHash);
        return JournalStatus::Ok;
    }
    // Skipping is just parsing frames
    template <class Stream>
    static JournalStatus deserialize(Stream& stream, FrameBatch& batch) {
        pack::HashingStream<Stream, utils::hash::Blake3Hasher> stream{
            baseStream};
        stream.enableHashing();

        auto header = journal::v1::FrameBatchStart{};
        schema::deserialize(stream, header);
        if (!stream.ok())
            return JournalStatus::Corruption;

        auto actualHeaderHash = stream.digest();
        utils::hash::Blake3Hasher::Hash expectedHeaderHash;
        schema::deserialize(stream, expectedHeaderHash);
        stream.require(expectedHeaderHash == actualHeaderHash,
                       ao::pack::Error::BadData);
        if (!stream.ok())
            return JournalStatus::Corruption;

        batch.frame = header.frame;

        // Reset and enable
        stream.enableHashing();

        // TODO add a cap on how much this can reserve
        batch.commands.reserve(header.commandCount);
        for (size_t i = 0; i < header.commandCount; ++i) {
            batch.commands.emplace_back();
            auto& p = batch.commands.back();
            schema::deserialize(stream, p.messageId);

            uint64_t messageSize = 0;
            schema::deserialize(stream, messageSize);
            if (!stream.ok())
                return JournalStatus::Corruption;

            p.payload.resize(messageSize);
            stream.bytes({p.payload.data(), messageSize}, messageSize);
        }

        auto payloadHash = stream.digest();
        auto expectedPayloadHash = utils::hash::Blake3Hasher::Hash{};
        schema::deserialize(stream, expectedPayloadHash);
        stream.require(payloadHash == expectedPayloadHash,
                       ao::pack::Error::BadData);
        if (!stream.ok())
            return JournalStatus::Corruption;

        return JournalStatus::Ok;
        // TODO we have to better map the error from the stream to a journal
        // status
    }
    template <class Stream>
    static JournalStatus skip(Stream& stream) {
        // We just deserialize but ignore the result
        FrameBatch out;
        return deserialize(stream, out);
    }
};

}  // namespace v1

using FrameIndex = uint64_t;

struct JournalStorageSettings {
    // 16MB per file
    size_t maxFileSize = 16 * 1024 * 1024;
};

struct AppendOptions {
    bool flush = false;
};

// Manages reads and writes to the log on disk
class JournalStorage {
   public:
    JournalStorage(JournalStorageSettings settings)
        : m_settings(std::move(settings)) {}

    void appendFrame(FrameBatch const& frameBatchStart,
                     AppendOptions opts = {});
    JournalStatus loadRoot(std::filesystem::path root);

    std::optional<FrameBatchView> nextFrame();
    std::optional<FrameBatchView> seekFrame(uint64_t frame);

   private:
    bool shouldRotateCurrentFile() const;
    bool rotateLogFile();
    size_t getLastFrame();

    JournalStatus m_status = JournalStatus::Ok;

    JournalStorageSettings m_settings;
    std::filesystem::path m_rootPath;

    std::unique_ptr<std::ifstream> m_readStream = nullptr;
    std::unique_ptr<std::ofstream> m_writeStream = nullptr;
    std::vector<std::pair<FrameIndex, std::filesystem::path>> m_fileFrames;

    size_t m_currentReadIndex = 0;
    size_t m_writeFileSize = 0;
    size_t m_nextWriteFrame = 0;
};

}  // namespace ao::journal

namespace ao::schema {
template <>
struct Serializer<journal::RecordKind>
    : public EnumSerializer<journal::RecordKind,
                            (size_t)journal::RecordKind::RecordKindMax,
                            uint32_t> {};

template <>
struct Serializer<journal::FrameBatch> {
    template <class Stream>
    void serialize(Stream& baseStream, journal::FrameBatch const& batch) {
        pack::HashingStream<Stream, utils::hash::Blake3Hasher> stream{
            baseStream};
        stream.enableHashing();

        auto header = journal::v1::FrameBatchStart{};
        header.frame = batch.frame;
        header.commandCount = batch.commands.size();
        header.payloadSize = 0;
        for (auto const& p : batch.commands) {
            // Add 8 for the size of the message id
            header.payloadSize += 8;
            header.payloadSize += p.payload.size();
        }

        schema::serialize(stream, header);

        auto headerHash = stream.digest();
        schema::serialize(stream, headerHash);

        // Reset hash and enable
        stream.enableHashing();

        for (auto const& p : batch.commands) {
            schema::serialize(stream, p.messageId);
            uint64_t size = p.payload.size();
            schema::serialize(stream, size);
            stream.bytes({p.payload.data(), p.payload.size()},
                         p.payload.size());
        }

        auto payloadHash = stream.digest();
        stream.disableHashing();
        schema::serialize(stream, payloadHash);
    }

    template <class Stream>
    void deserialize(Stream& baseStream, journal::FrameBatch& batch) {}
};

}  // namespace ao::schema
