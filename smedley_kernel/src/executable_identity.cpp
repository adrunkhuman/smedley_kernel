#include <smedley/executable_identity.hpp>

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace smedley
{
    namespace
    {
        constexpr uintmax_t supported_executable_size = 12294656;
        constexpr std::array<unsigned char, 32> supported_executable_hash{
            0x62, 0xd4, 0x8c, 0x20, 0x43, 0x64, 0xdd, 0x70,
            0x65, 0x84, 0x77, 0x7c, 0x2e, 0x2b, 0x3c, 0x7a,
            0xb3, 0xc5, 0xf1, 0xdd, 0x01, 0x70, 0x87, 0x25,
            0x54, 0x94, 0x35, 0x75, 0xd5, 0x3d, 0x66, 0x48,
        };
        std::once_flag validation_once;
        std::atomic<bool> executable_supported{};

        bool HashMatches(const std::filesystem::path &path)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            std::vector<unsigned char> hash_object;
            auto cleanup = [&] {
                if (hash != nullptr) BCryptDestroyHash(hash);
                if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
            };
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
            DWORD object_size = 0;
            DWORD copied = 0;
            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                    sizeof(object_size), &copied, 0) < 0) {
                cleanup();
                return false;
            }
            hash_object.resize(object_size);
            if (BCryptCreateHash(algorithm, &hash, hash_object.data(), object_size, nullptr, 0, 0) < 0) {
                cleanup();
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            std::array<char, 64 * 1024> buffer{};
            while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
                if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                        static_cast<ULONG>(input.gcount()), 0) < 0) {
                    cleanup();
                    return false;
                }
            }
            std::array<unsigned char, 32> actual_hash{};
            const bool matches = input.eof()
                && BCryptFinishHash(hash, actual_hash.data(), actual_hash.size(), 0) >= 0
                && actual_hash == supported_executable_hash;
            cleanup();
            return matches;
        }

        bool ValidateExecutable()
        {
            std::array<wchar_t, 32768> path_buffer{};
            const DWORD length = GetModuleFileNameW(nullptr, path_buffer.data(), static_cast<DWORD>(path_buffer.size()));
            if (length == 0 || length == path_buffer.size()) return false;
            const std::filesystem::path path(std::wstring_view(path_buffer.data(), length));
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error
                || std::filesystem::file_size(path, error) != supported_executable_size || error) return false;
            return HashMatches(path);
        }
    }

    bool ValidateCurrentExecutableIdentity()
    {
        std::call_once(validation_once, [] {
            try {
                executable_supported.store(ValidateExecutable(), std::memory_order_release);
            } catch (...) {
                executable_supported.store(false, std::memory_order_release);
            }
        });
        return IsCurrentExecutableSupported();
    }

    bool IsCurrentExecutableSupported() noexcept
    {
        return executable_supported.load(std::memory_order_acquire);
    }
}
