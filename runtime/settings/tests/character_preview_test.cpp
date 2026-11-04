#include "settings/CharacterPreview.h"
#include <bit>
#include <cassert>
#include <cmath>

int main() {
    std::vector<uint8_t> bytes;
    const auto u32 = [&](uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(uint8_t(value >> (8 * i)));
    };
    const auto f32 = [&](float value) { u32(std::bit_cast<uint32_t>(value)); };
    u32(0x4d504344);
    u32(1);
    u32(3);
    u32(1);
    u32(1);
    for (float value : {-0.1f, -0.2f, 0.0f, 0.1f, -0.2f, 0.0f, 0.0f, 0.2f, 0.0f})
        f32(value);
    u32(0);
    u32(1);
    u32(2);
    u32(0);
    for (unsigned i = 0; i < 6; ++i)
        f32(0);
    u32(1);
    u32(1);
    u32(0xffffffff);
    auto model = dsettings::CharacterPreview::decode(bytes);
    assert(model && model->triangleCount() == 1 && !model->ready());
    assert(model->upload(
        [](const void*, uint32_t w, uint32_t h) {
            assert(w == 1 && h == 1);
            return ImTextureID(77);
        },
        [](ImTextureID) {}));
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::NewFrame();
    auto* list = ImGui::GetBackgroundDrawList();
    model->draw(list, {0, 0, 600, 900}, 0, 1);
    assert(list->VtxBuffer.Size == 3);
    const float first = list->VtxBuffer[0].pos.x;
    model->draw(list, {0, 0, 600, 900}, 0.01, 1);
    assert(list->VtxBuffer.Size == 6 && list->VtxBuffer[3].pos.x != first); // continuous, not a frame strip
    for (const auto& vertex : list->VtxBuffer)
        assert(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y));
    ImGui::EndFrame();
    ImGui::DestroyContext();
    unsigned released = 0;
    model->release([&](ImTextureID) { ++released; });
    assert(released == 1 && !model->ready());
    for (size_t size = 0; size < bytes.size(); ++size)
        assert(!dsettings::CharacterPreview::decode({bytes.begin(), bytes.begin() + size}));
    bytes.push_back(0);
    assert(!dsettings::CharacterPreview::decode(bytes));
}
