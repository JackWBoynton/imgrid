

// Dear ImGui: standalone example application for GLFW + OpenGL 3, using
// programmable pipeline (GLFW is a cross-platform general purpose library for
// handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation,
// etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/
// folder).
// - Introduction, links and more at the top of imgui.cpp

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "implot.h"
#include "implot_demo.cpp"

#include "imgrid.h"

#include <map>
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to
// maximize ease of testing and compatibility with old VS compilers. To link
// with VS2010-era libraries, VS2015+ requires linking with
// legacy_stdio_definitions.lib, which we do using this pragma. Your own project
// should not be affected, as you are likely to link with a newer binary of GLFW
// that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) &&                                 \
    !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See
// 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

static void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

struct GuageColorMap {
  std::map<float, ImU32> Map;

  static bool Render();

  GuageColorMap(std::initializer_list<std::pair<const float, ImU32>> init)
      : Map(init) {}
  GuageColorMap() = default;

  static GuageColorMap *Editing;
};

GuageColorMap *GuageColorMap::Editing = nullptr;

bool GuageColorMap::Render() {
  bool set = false;
  if (ImGui::BeginPopup("ColorMap Popup")) {
    printf("Asdfasdf\n");

    if (GuageColorMap::Editing == nullptr) {
      ImGui::EndPopup();
      return false;
    }

    for (auto &[key, color] : GuageColorMap::Editing->Map) {
      ImGui::PushID(key);
      ImGui::PushItemWidth(100);
      // Color pickers do not take ImU32 so we need to convert them to vec4 and
      // back
      auto color_vec4 = ImColor(color).Value;
      if (ImGui::ColorPicker3("Color", &color_vec4.x)) {
        color = ImColor(color_vec4);
      }
      ImGui::PopItemWidth();
      ImGui::SameLine();
      ImGui::InputFloat("Value", (float *)&key);
      ImGui::PopID();
    }

    if (ImGui::Button("Confirm")) {
      set = true;
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  return set;
}

bool SimpleGuage(const char *label, float value, float min, float max,
                 GuageColorMap &colorMap, const char *format = "%.2f",
                 float radius = 50, float thickness = 10,
                 float start_angle = 0.75f, float end_angle = 2.25f,
                 float threshold_indicator_div = 6.0f) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return false;

  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;
  const ImGuiID id = window->GetID(label);
  ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

  const ImVec2 pos = window->DC.CursorPos;

  // bool needs_wrap = label_size.x > radius * 2 - style.FramePadding.x * 2;

  // if (needs_wrap) {
  //   label_size.y += (int)(label_size.x / (radius * 2)) * CalcTextSize("A").y;
  // }

  const ImRect total_bb(pos,
                        pos + ImVec2(radius * 2, radius * 2) +
                            ImVec2(0, label_size.y + style.FramePadding.y) +
                            style.FramePadding * 2);
  ImGui::ItemSize(total_bb, style.FramePadding.y);
  if (!ImGui::ItemAdd(total_bb, id)) {
    return false;
  }

  bool hovered, held;
  bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held,
                                       ImGuiButtonFlags_MouseButtonRight);
  if (pressed) {
    ImGui::MarkItemEdited(id);
    GuageColorMap::Editing = &colorMap;
    ImGui::OpenPopup("ColorMap Popup", ImGuiPopupFlags_AnyPopup);
  }

  auto label_pos =
      total_bb.GetTL() +
      ImVec2((total_bb.GetWidth() - label_size.x) / 2, style.FramePadding.y);

  // add the label above the guage in the center
  window->DrawList->AddText(label_pos, ImColor(style.Colors[ImGuiCol_Text]),
                            label);
  const auto center =
      total_bb.GetCenter() + ImVec2(0, label_size.y + style.ItemInnerSpacing.y);

  // lambda to convert from value to angle ( corrected for the start of 0.75 and
  // end of 2.25 )
  auto lerper = [&](float val) {
    return start_angle * IM_PI +
           (val - min) / (max - min) * (end_angle - start_angle) * IM_PI;
  };

  auto color_ring_thickness = thickness / threshold_indicator_div;
  auto color_ring_radius = radius;

  // add the colors
  float current_angle = start_angle * IM_PI;
  for (const auto &[key, color] : colorMap.Map) {
    float next_angle = lerper(key);
    window->DrawList->PathClear();
    window->DrawList->PathArcTo(center, color_ring_radius, current_angle,
                                next_angle);
    window->DrawList->PathStroke(color, ImDrawFlags_None, color_ring_thickness);
    current_angle = next_angle;
  }

  // add the background of the guage
  auto value_ring_radius =
      radius - color_ring_thickness * 3.0f - radius / (16.0f);
  window->DrawList->PathClear();
  window->DrawList->PathArcTo(center, value_ring_radius, start_angle * IM_PI,
                              end_angle * IM_PI);
  window->DrawList->PathStroke(ImColor(style.Colors[ImGuiCol_MenuBarBg]),
                               ImDrawFlags_None, thickness);

  // add the current values bar of the correct color and angle
  float current_angle_value =
      std::max(start_angle * IM_PI, std::min(end_angle * IM_PI, lerper(value)));
  window->DrawList->PathClear();
  window->DrawList->PathArcTo(center, value_ring_radius, start_angle * IM_PI,
                              current_angle_value);
  ImU32 value_color = 0;
  ImU32 last_color = 0;
  for (const auto &[key, color] : colorMap.Map) {
    if (value <= key) {
      value_color = color;
      break;
    }
    last_color = color;
  }
  if (value_color == 0) {
    value_color = last_color;
  }
  window->DrawList->PathStroke(value_color, ImDrawFlags_None, thickness);

  // add a white line at the end of the value to make it look like a needle
  auto value_ring_thickness = thickness + 1.0f;
  window->DrawList->PathClear();
  window->DrawList->PathArcTo(center, value_ring_radius,
                              current_angle_value - 0.01f * IM_PI / 2,
                              current_angle_value + 0.01f * IM_PI / 2);
  window->DrawList->PathStroke(ImColor(ImVec4(1, 1, 1, 1)), ImDrawFlags_None,
                               value_ring_thickness);

  // add the value in the middle of the guage
  char buffer[64];
  snprintf(buffer, sizeof(buffer), format, value);
  const ImVec2 value_size = ImGui::CalcTextSize(buffer, nullptr, true);
  window->DrawList->AddText(center - value_size / 2, value_color, buffer);

  return true;
}

// Main code
int main(int, char **) {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
    return 1;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
  // GL 3.2 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 3.2+ only
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on Mac
#else
  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+
  // only glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // 3.0+ only
#endif

  // Create window with graphics context
  GLFWwindow *window = glfwCreateWindow(
      1280, 720, "Dear ImGui GLFW+OpenGL3 example", nullptr, nullptr);
  if (window == nullptr)
    return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGrid::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;            // Enable Gamepad Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();
  //

  ImFontConfig font_cfg;
  font_cfg.SizePixels = 26.0f;
  auto *default_font = io.Fonts->AddFontDefault();
  (void)default_font;
  auto *big_font = io.Fonts->AddFontDefault(&font_cfg);

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
  ImGui_ImplGlfw_InstallEmscriptenCanvasResizeCallback("#canvas");
#endif
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can
  // also load multiple fonts and use ImGui::PushFont()/PopFont() to select
  // them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you
  // need to select the font among multiple.
  // - If the file cannot be loaded, the function will return a nullptr. Please
  // handle those errors in your application (e.g. use an assertion, or display
  // an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and stored
  // into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which
  // ImGui_ImplXXXX_NewFrame below will call.
  // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype
  // for higher quality font rendering.
  // - Read 'docs/FONTS.md' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string
  // literal you need to write a double backslash \\ !
  // - Our Emscripten build process allows embedding fonts to be accessible at
  // runtime from the "fonts/" folder. See Makefile.emscripten for details.
  // io.Fonts->AddFontDefault();
  // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  // ImFont* font =
  // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f,
  // nullptr, io.Fonts->GetGlyphRangesJapanese()); IM_ASSERT(font != nullptr);

  // Our state
  bool show_demo_window = true;
  bool show_another_window = false;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  // Main loop
#ifdef __EMSCRIPTEN__
  // For an Emscripten build we are disabling file-system access, so let's not
  // attempt to do a fopen() of the imgui.ini file. You may manually call
  // LoadIniSettingsFromMemory() to load settings from your own storage.
  io.IniFilename = nullptr;
  EMSCRIPTEN_MAINLOOP_BEGIN
#else
  while (!glfwWindowShouldClose(window))
#endif
  {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
    // tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
    // your main application, or clear/overwrite your copy of the mouse data.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
    // data to your main application, or clear/overwrite your copy of the
    // keyboard data. Generally you may always pass all inputs to dear imgui,
    // and hide them from your application based on those two flags.
    glfwPollEvents();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    static GuageColorMap colorMap = {{50, IM_COL32(0, 153, 0, 255)},
                                     {75, IM_COL32(255, 255, 0, 255)},
                                     {100, IM_COL32(255, 0, 0, 255)}};

    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    [[maybe_unused]] static float f = 0;
    static float radius = 100.0f;
    static float thickness = 10.0f;
    ImGrid::PushStyleVar(ImGridStyleVar_GridSpacing, 70.0f);
    if (ImGui::Begin("Grid")) {

      ImGrid::BeginGrid();
      ImGrid::BeginEntry(0);
      {
        ImGrid::BeginEntryTitleBar();
        ImGui::Text("Entry 0");
        ImGrid::EndEntryTitleBar();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("float", &f, 0.0f, 100.0f);
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("radius", &radius, 0.0f, 300.0f);
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("thickness", &thickness, 0.0f, 50.0f);

        SimpleGuage("Guage", f, 0.0f, 100.0f, colorMap, "%.2f", radius,
                    thickness);
      }
      ImGrid::EndEntry();
      int i = 1;
      for (i = 1; i < 3; i++) {
        ImGrid::BeginEntry(i);
        {
          ImGrid::BeginEntryTitleBar();
          ImGui::Text("Entry %d", i);
          ImGrid::EndEntryTitleBar();
          ImGui::Text("Entry %d content", i);

          ImGui::SetNextItemWidth(100);
          ImGui::SliderFloat("float", &f, 0.0f, 100.0f);
          ImGui::SetNextItemWidth(100);
          ImGui::SliderFloat("radius", &radius, 0.0f, 300.0f);
          ImGui::SetNextItemWidth(100);
          ImGui::SliderFloat("thickness", &thickness, 0.0f, 50.0f);

          SimpleGuage("Guage", f, 0.0f, 100.0f, colorMap, "%.2f", radius,
                      thickness);
        }
        ImGrid::EndEntry();
      }

      for (; i < 5; i++) {
        ImGrid::BeginEntry(i);
        {
          ImGrid::BeginEntryTitleBar();
          ImGui::Text("Entry %d", i);
          ImGrid::EndEntryTitleBar();
          ImGui::Text("Entry %d content", i);

          ImPlot::PushStyleVar(ImPlotStyleVar_PlotDefaultSize,
                               ImVec2(300, 300));
          ImPlot::Demo_LinePlots();
          ImPlot::PopStyleVar();
        }
        ImGrid::EndEntry();
      }

      for (; i < 6; i++) {
        ImGrid::BeginEntry(i);
        {
          ImGrid::BeginEntryTitleBar();
          ImGui::Text("Entry %d", i);
          ImGrid::EndEntryTitleBar();
          ImGui::Text("Entry %d content", i);

          ImPlot::PushStyleVar(ImPlotStyleVar_PlotDefaultSize,
                               ImVec2(400, 200));
          ImPlot::Demo_RealtimePlots();
          ImPlot::PopStyleVar();
        }
        ImGrid::EndEntry();
      }
      for (; i < 8; i++) {
        ImGrid::BeginEntry(i);
        {
          ImGrid::BeginEntryTitleBar();
          ImGui::Text("Entry %d", i);
          ImGrid::EndEntryTitleBar();
          ImGui::Text("Entry %d content", i);

          ImGui::PushFont(big_font);
          ImGui::Text("%f", f);
          ImGui::PopFont();
        }
        ImGrid::EndEntry();
      }

      ImGrid::EndGrid();
      GuageColorMap::Render(); // this will render the color map popup (only
                               // opened if a guage is clicked)
    }
    ImGui::End();
    ImGrid::PopStyleVar();

    if (ImGui::Begin("Grid Debug")) {
      ImGrid::RenderDebug();
    }
    ImGui::End();

    // 1. Show the big demo window (Most of the sample code is in
    // ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear
    // ImGui!).
    if (show_demo_window)
      ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair
    // to create a named window.
    {
      static float f = 0.0f;
      static int counter = 0;

      ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!"
                                     // and append into it.

      ImGui::Text("This is some useful text."); // Display some text (you can
                                                // use a format strings too)
      ImGui::Checkbox(
          "Demo Window",
          &show_demo_window); // Edit bools storing our window open/close state
      ImGui::Checkbox("Another Window", &show_another_window);

      ImGui::SliderFloat("float", &f, 0.0f,
                         1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
      ImGui::ColorEdit3(
          "clear color",
          (float *)&clear_color); // Edit 3 floats representing a color

      if (ImGui::Button("Button")) // Buttons return true when clicked (most
                                   // widgets return true when edited/activated)
        counter++;
      ImGui::SameLine();
      ImGui::Text("counter = %d", counter);

      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                  1000.0f / io.Framerate, io.Framerate);

      ImGui::End();
    }

    // 3. Show another simple window.
    if (show_another_window) {
      ImGui::Begin(
          "Another Window",
          &show_another_window); // Pass a pointer to our bool variable (the
                                 // window will have a closing button that will
                                 // clear the bool when clicked)
      ImGui::Text("Hello from another window!");
      if (ImGui::Button("Close Me"))
        show_another_window = false;
      ImGui::End();
    }

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }
#ifdef __EMSCRIPTEN__
  EMSCRIPTEN_MAINLOOP_END;
#endif

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
