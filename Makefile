CXX = g++
CXXFLAGS = -std=c++23 -O2 -include windows.h
LDFLAGS = -mwindows -s

# Include paths
INCLUDES = -Iimgui -Iimgui/backends

# Libraries
LIBS = -ld3d11 -ldwmapi -ld3dcompiler -lgdi32 -lz

# Source files
SRC = \
main.cpp \
imgui/imgui.cpp \
imgui/imgui_draw.cpp \
imgui/imgui_tables.cpp \
imgui/imgui_widgets.cpp \
imgui/imgui_demo.cpp \
imgui/backends/imgui_impl_dx11.cpp \
imgui/backends/imgui_impl_win32.cpp \
minIni.c

OBJ = $(SRC:.cpp=.o)

TARGET = imgui_app

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $(TARGET) $(LIBS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
