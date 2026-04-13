CXX      = g++
CXXFLAGS = -std=c++11 -g -Wall

IMGUI_DIR = imgui
IMGUI_SRC = \
    $(IMGUI_DIR)/imgui.cpp \
    $(IMGUI_DIR)/imgui_draw.cpp \
    $(IMGUI_DIR)/imgui_tables.cpp \
    $(IMGUI_DIR)/imgui_widgets.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

INCS = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I..
LIBS = -lglfw -lGL -lpthread -ldl

SRCS = main.cpp ChatNetworkClient.cpp ChatUI.cpp $(IMGUI_SRC)

TARGET = chat_client

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(INCS) $(SRCS) $(LIBS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: clean
