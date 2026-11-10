CXX := g++
WINDRES := windres
CPPFLAGS := -Iinclude -Ithird_party/rtaudio -Ithird_party/rtaudio/include -Ithird_party/imgui -Ithird_party/imgui/backends -D__WINDOWS_DS__ -D__WINDOWS_ASIO__ -DNOMINMAX -D_WIN32_WINNT=0x0A00
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -MMD -MP
CPPFLAGS += -Ithird_party/kissfft -Ithird_party/dr_libs -Dkiss_fft_scalar=double
LDFLAGS := -static -static-libgcc -static-libstdc++
LDLIBS := -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -ldsound -lwinmm -lole32 -luuid -lpropsys -lksuser -luser32 -lgdi32 -limm32 -lavrt -lwindowscodecs
CORE := src/dsp.cpp src/device.cpp src/audio.cpp third_party/rtaudio/RtAudio.cpp third_party/rtaudio/include/asio.cpp third_party/rtaudio/include/asiodrivers.cpp third_party/rtaudio/include/asiolist.cpp third_party/rtaudio/include/iasiothiscallresolver.cpp
UI := src/gui_main.cpp src/ui.cpp third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_win32.cpp third_party/imgui/backends/imgui_impl_dx11.cpp
COREOBJ := $(CORE:%.cpp=build/%.o)
ANALYSISOBJ := build/src/analyzer/analysis.o build/src/analyzer/files.o build/third_party/kissfft/kiss_fft.o
CAPTUREOBJ := build/src/analyzer/capture.o
UIOBJ := $(UI:%.cpp=build/%.o)
.PHONY: all test
all: dist/ToneLab.exe dist/tonelab-cli.exe dist/dsp-tests.exe dist/loopback-test.exe dist/ToneScope.exe dist/tonescope-cli.exe dist/analysis-tests.exe
dist/ToneScope.exe: $(filter-out build/src/audio.o,$(COREOBJ)) $(ANALYSISOBJ) $(CAPTUREOBJ) build/src/analyzer/ui.o build/src/analyzer/gui_main.o $(filter-out build/src/gui_main.o build/src/ui.o,$(UIOBJ)) build/tonescope-resource.o
	$(CXX) $(LDFLAGS) -mwindows -o $@ $^ $(LDLIBS) -lcomdlg32 -lshell32
dist/tonescope-cli.exe: $(ANALYSISOBJ) $(CAPTUREOBJ) build/src/analyzer/cli.o build/tonescope-resource.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
dist/analysis-tests.exe: $(ANALYSISOBJ) build/tests/analysis_tests.o build/tonelab-resource.o
	$(CXX) $(LDFLAGS) -o $@ $^
build/src/analyzer/gui_main.o: src/gui_main.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DTONE_ANALYZER -c $< -o $@
build/third_party/kissfft/kiss_fft.o: third_party/kissfft/kiss_fft.c
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
dist/ToneLab.exe: $(COREOBJ) $(UIOBJ) build/tonelab-resource.o
	$(CXX) $(LDFLAGS) -mwindows -o $@ $^ $(LDLIBS)
dist/tonelab-cli.exe: $(COREOBJ) build/src/cli.o build/tonelab-resource.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
dist/dsp-tests.exe: build/src/dsp.o build/tests/dsp_tests.o build/tonelab-resource.o
	$(CXX) $(LDFLAGS) -o $@ $^
dist/loopback-test.exe: $(COREOBJ) build/tests/loopback.o build/tonelab-resource.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
build/%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
build/tonelab-resource.o: resources/tonelab.rc assets/tonelab.ico
	$(WINDRES) -Iassets -i $< -o $@ -O coff
build/tonescope-resource.o: resources/tonelab.rc assets/tonelab.ico
	$(WINDRES) -DTONE_SCOPE_RESOURCE -Iassets -i $< -o $@ -O coff
test: dist/dsp-tests.exe
	./dist/dsp-tests.exe
-include $(COREOBJ:.o=.d) $(UIOBJ:.o=.d) build/src/cli.d build/tests/dsp_tests.d build/tests/loopback.d
-include $(ANALYSISOBJ:.o=.d) $(CAPTUREOBJ:.o=.d) build/src/analyzer/ui.d build/src/analyzer/gui_main.d build/src/analyzer/cli.d build/tests/analysis_tests.d
