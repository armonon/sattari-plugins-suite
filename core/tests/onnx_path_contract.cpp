#include <sattari_audio_core/SattariOnnxPath.h>

#include <cstdio>
#include <type_traits>

int main()
{
    const std::string utf8Path = u8"C:/Sattari Audio/\u97f3\u58f0/\u00e9-\U0001d11e/model.onnx";
    const auto nativePath = sattari::onnx::nativeModelPath (utf8Path);
   #if defined(_WIN32)
    static_assert (std::is_same_v<std::decay_t<decltype(nativePath)>, std::wstring>);
    const std::wstring expected = L"C:/Sattari Audio/\u97f3\u58f0/\u00e9-\U0001d11e/model.onnx";
   #else
    static_assert (std::is_same_v<std::decay_t<decltype(nativePath)>, std::string>);
    const auto& expected = utf8Path;
   #endif
    if (nativePath != expected
        || ! sattari::onnx::nativeModelPath ("").empty()
        || sattari::onnx::nativeModelPath ("model.onnx").size() != 10)
    {
        std::puts ("ONNX native path contract: FAILED");
        return 1;
    }
    std::puts ("ONNX native path contract: OK");
    return 0;
}
