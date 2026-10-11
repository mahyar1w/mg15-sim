#include "amp.h"
#include "audio.h"
#include "user_ir_data.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct App {
    amp::Audio audio;
    amp::Params params;
    std::vector<amp::Device> ins, outs;
    int in_pick = 0, out_pick = 0;
    int buffer = 256;
    std::string status;
};

void refresh(App& app) {
    app.ins = app.audio.inputs();
    app.outs = app.audio.outputs();
    app.in_pick = std::clamp(app.in_pick, 0, std::max(0, static_cast<int>(app.ins.size()) - 1));
    app.out_pick = std::clamp(app.out_pick, 0, std::max(0, static_cast<int>(app.outs.size()) - 1));
}

void meter(const char* label, float peak) {
    ImGui::ProgressBar(peak, ImVec2(-1, 0), label);
}

void audio_settings(App& app) {
    if (!ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (app.audio.running()) {
        if (ImGui::Button("Stop")) app.audio.stop();
        return;
    }

    if (!app.ins.empty()) {
        std::vector<const char*> names;
        for (const auto& d : app.ins) names.push_back(d.name.c_str());
        ImGui::Combo("Input", &app.in_pick, names.data(), static_cast<int>(names.size()));
    }
    if (!app.outs.empty()) {
        std::vector<const char*> names;
        for (const auto& d : app.outs) names.push_back(d.name.c_str());
        ImGui::Combo("Output", &app.out_pick, names.data(), static_cast<int>(names.size()));
    }
    ImGui::SliderInt("Buffer", &app.buffer, 64, 1024, "%d", ImGuiSliderFlags_AlwaysClamp);

    if (ImGui::Button("Start")) {
        const int in = app.ins.empty() ? -1 : app.ins[app.in_pick].index;
        const int out = app.outs.empty() ? -1 : app.outs[app.out_pick].index;
        if (!app.audio.start(in, out, 48000.0, static_cast<unsigned long>(app.buffer)))
            app.status = app.audio.error();
        else
            app.status.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refresh(app);
}

void panel(App& app) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##fullscreen", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);

    ImGui::TextUnformatted("Marshall MG15CF");
    ImGui::Spacing();

    // Channel
    ImGui::PushItemWidth(-1);
    if (ImGui::RadioButton("Clean", !app.params.drive)) app.params.drive = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Overdrive", app.params.drive)) app.params.drive = true;
    ImGui::Spacing();

    ImGui::BeginDisabled(app.params.drive);
    ImGui::SliderFloat("##clean", &app.params.clean_volume, 0, 10, "Clean volume  %.1f");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!app.params.drive);
    ImGui::SliderFloat("##gain", &app.params.gain, 0, 10, "Gain  %.1f");
    ImGui::SliderFloat("##drivevolume", &app.params.drive_volume, 0, 10, "Overdrive volume  %.1f");
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("Amp -> EQ -> IR");
    ImGui::TextUnformatted("Shared EQ");
    ImGui::SliderFloat("##bass", &app.params.bass, 0, 10, "Bass  %.1f");
    ImGui::SliderFloat("##middle", &app.params.middle, 0, 10, "Middle  %.1f");
    ImGui::SliderFloat("##treble", &app.params.treble, 0, 10, "Treble  %.1f");
    ImGui::Spacing();
    ImGui::Checkbox("Output voicing", &app.params.voicing);
    ImGui::BeginDisabled(!app.params.voicing);
    if (ImGui::RadioButton("Guitar speaker", app.params.speaker_ir)) app.params.speaker_ir = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Measured headphones", !app.params.speaker_ir)) app.params.speaker_ir = false;
    if (app.params.speaker_ir) {
        std::vector<const char*> cabs{"MG15CF (built-in)"};
        for (int i = 0; i < amp::kUserIrCount; ++i) cabs.push_back(amp::kUserIrNames[i]);
        ImGui::Combo("##cabinet", &app.params.cabinet, cabs.data(), static_cast<int>(cabs.size()));
        ImGui::TextDisabled("Smoothed MG15CF EQ / selected speaker IR");
    }
    ImGui::EndDisabled();
    ImGui::SliderFloat("##output", &app.params.output_db, -36, 6, "Output trim  %.1f dB");
    ImGui::PopItemWidth();
    ImGui::Spacing();

    // Meters
    meter("In ", app.audio.input_peak());
    meter("Out", app.audio.output_peak());
    ImGui::Spacing();

    audio_settings(app);

    if (!app.status.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", app.status.c_str());
    }

    app.audio.set_params(app.params);
    ImGui::End();
}

} // namespace

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(450, 660, "MG15CF", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(16, 14);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app;
    refresh(app);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        panel(app);

        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    app.audio.stop();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
