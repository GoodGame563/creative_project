// ============================================================================
// NEON CYBER-CITY AT NIGHT - OpenGL Project (GLAD version)
// ============================================================================
// Высокие небоскрёбы, летающие машины, голограммы, мокрый асфальт с отражениями
// Много цветных неоновых источников света (point + spot)
// Анимированные рекламные щиты, мигающие огни, пар из канализации, пролетающие дроны
// Сильный bloom и glow эффекты
// Камера может летать между зданиями или ехать по улице
// 2D: футуристический интерфейс с бегущей строкой и сканлайнами
// ============================================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/noise.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <string>
#include <algorithm>

// ============================================================================
// Constants
// ============================================================================
const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;
const int MAX_BUILDINGS = 50;
const int MAX_FLYING_CARS = 20;
const int MAX_DRONES = 15;
const int MAX_STEAM_EMITTERS = 10;
const int MAX_NEON_LIGHTS = 30;
const int MAX_BILLBOARDS = 10;

// ============================================================================
// Global Variables
// ============================================================================
GLFWwindow* window = nullptr;
GLuint cityVAO, cityVBO, cityInstanceVBO;
GLuint roadVAO, roadVBO;
GLuint carVAO, carVBO, carInstanceVBO;
GLuint droneVAO, droneVBO, droneInstanceVBO;
GLuint billboardVAO, billboardVBO, billboardInstanceVBO;
GLuint steamVAO, steamVBO, steamInstanceVBO;
GLuint quadVAO, quadVBO;
GLuint hdrFBO, hdrColorbuffer[2], pingpongFBO[2], pingpongColorbuffer[2];
GLuint sceneShader, bloomExtractShader, blurShader, bloomCompositeShader, uiShader;

float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

glm::vec3 cameraPos(0.0f, 5.0f, 20.0f);
glm::vec3 cameraFront(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);
float yaw = -90.0f;
float pitch = 0.0f;
float moveSpeed = 10.0f;
int cameraMode = 0; // 0: Fly, 1: Drive

float globalTime = 0.0f;

struct Building {
    glm::vec3 position;
    glm::vec3 size;
    glm::vec3 windowColor;
    float windowBlinkOffset;
};

struct FlyingCar {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float lane;
    float blinkOffset;
};

struct Drone {
    glm::vec3 position;
    glm::vec3 target;
    float speed;
    float hoverHeight;
    float blinkOffset;
};

struct SteamEmitter {
    glm::vec3 position;
    float emissionTimer;
};

struct NeonLight {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float blinkSpeed;
    float blinkOffset;
};

struct Billboard {
    glm::vec3 position;
    float rotation;
    int pattern;
    float animTimer;
};

std::vector<Building> buildings;
std::vector<FlyingCar> flyingCars;
std::vector<Drone> drones;
std::vector<SteamEmitter> steamEmitters;
std::vector<NeonLight> neonLights;
std::vector<Billboard> billboards;

// ============================================================================
// Shader Sources
// ============================================================================
const char* sceneVertexSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform int objectType; // 0: building, 1: car, 2: drone, 3: billboard, 4: steam

out vec3 FragPos;
out vec3 Normal;
out vec3 Color;
out vec2 TexCoord;
out float Visibility;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    
    if (objectType == 2) { // Drone - add hover animation
        worldPos.xyz += vec3(0.0, sin(time * 3.0) * 0.2, 0.0);
    }
    
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    Color = aColor;
    TexCoord = aTexCoord;
    
    vec4 viewPos = view * worldPos;
    gl_Position = projection * viewPos;
    
    // Fog for depth
    float dist = length(viewPos.xyz);
    Visibility = exp(-0.0002 * dist * dist);
}
)";

const char* sceneFragmentSource = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 Color;
in vec2 TexCoord;
in float Visibility;

uniform vec3 viewPos;
uniform float time;
uniform int objectType;
uniform vec3 lightColors[30];
uniform vec3 lightPositions[30];
uniform float lightIntensities[30];
uniform int numLights;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);
    
    // Ambient
    vec3 ambient = 0.02 * Color;
    
    // Emissive for neon objects
    vec3 emissive = vec3(0.0);
    if (objectType == 0) { // Buildings with neon edges
        float edgeFactor = 1.0 - abs(dot(normal, vec3(0.0, 1.0, 0.0)));
        emissive = Color * edgeFactor * 0.5;
    } else if (objectType == 1) { // Cars
        emissive = Color * 0.8;
    } else if (objectType == 2) { // Drones
        float blink = sin(time * 10.0 + TexCoord.x * 100.0) * 0.5 + 0.5;
        emissive = Color * blink;
    } else if (objectType == 3) { // Billboards
        float pattern = sin(TexCoord.y * 20.0 + time * 2.0) * cos(TexCoord.x * 20.0 + time);
        emissive = Color * (pattern * 0.5 + 0.5) * 1.5;
    }
    
    // Lighting from point lights
    vec3 lighting = ambient + emissive;
    
    for (int i = 0; i < numLights && i < 30; i++) {
        vec3 lightPos = lightPositions[i];
        vec3 lightDir = normalize(lightPos - FragPos);
        float dist = length(lightPos - FragPos);
        float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = lightColors[i] * diff * lightIntensities[i] * attenuation;
        
        vec3 reflectDir = reflect(-lightDir, normal);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = lightColors[i] * spec * attenuation * 0.3;
        
        lighting += diffuse + specular;
    }
    
    // Wet road reflection effect
    if (objectType == 4 || objectType == 5) { // Road or ground
        float fresnel = pow(1.0 - max(dot(viewDir, normal), 0.0), 3.0);
        lighting += vec3(0.1, 0.2, 0.3) * fresnel * 0.5;
    }
    
    // Apply fog
    vec3 finalColor = mix(vec3(0.02, 0.02, 0.05), lighting, Visibility);
    
    FragColor = vec4(finalColor, 1.0);
}
)";

const char* bloomExtractVertexSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* bloomExtractFragmentSource = R"(
#version 330 core
in vec2 TexCoord;
uniform sampler2D sceneTexture;
uniform float threshold;

out vec4 FragColor;

void main() {
    vec3 color = texture(sceneTexture, TexCoord).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 brightColor = max(color - threshold, vec3(0.0));
    FragColor = vec4(brightColor, 1.0);
}
)";

const char* blurVertexSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* blurFragmentSource = R"(
#version 330 core
in vec2 TexCoord;
uniform sampler2D image;
uniform bool horizontal;
uniform vec2 texelSize;

out vec4 FragColor;

void main() {
    vec3 result = texture(image, TexCoord).rgb;
    if (horizontal) {
        for (int i = 1; i <= 4; i++) {
            result += texture(image, TexCoord + vec2(texelSize.x * i, 0.0)).rgb * 0.5;
            result += texture(image, TexCoord - vec2(texelSize.x * i, 0.0)).rgb * 0.5;
        }
    } else {
        for (int i = 1; i <= 4; i++) {
            result += texture(image, TexCoord + vec2(0.0, texelSize.y * i)).rgb * 0.5;
            result += texture(image, TexCoord - vec2(0.0, texelSize.y * i)).rgb * 0.5;
        }
    }
    FragColor = vec4(result / 9.0, 1.0);
}
)";

const char* bloomCompositeVertexSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* bloomCompositeFragmentSource = R"(
#version 330 core
in vec2 TexCoord;
uniform sampler2D sceneTexture;
uniform sampler2D bloomTexture;

out vec4 FragColor;

void main() {
    vec3 sceneColor = texture(sceneTexture, TexCoord).rgb;
    vec3 bloomColor = texture(bloomTexture, TexCoord).rgb;
    
    // Tone mapping
    sceneColor = sceneColor / (sceneColor + vec3(1.0));
    bloomColor = bloomColor / (bloomColor + vec3(1.0));
    
    // Gamma correction
    sceneColor = pow(sceneColor, vec3(1.0/2.2));
    bloomColor = pow(bloomColor, vec3(1.0/2.2));
    
    vec3 finalColor = sceneColor + bloomColor;
    FragColor = vec4(finalColor, 1.0);
}
)";

const char* uiVertexSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

uniform mat4 projection;

out vec2 TexCoord;
out vec4 Color;

void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}
)";

const char* uiFragmentSource = R"(
#version 330 core
in vec2 TexCoord;
in vec4 Color;

uniform float time;
uniform int elementType; // 0: scanline, 1: text, 2: bar

out vec4 FragColor;

void main() {
    vec4 finalColor = Color;
    
    if (elementType == 0) { // Scanlines
        float scanline = sin((TexCoord.y + time * 0.5) * 100.0) * 0.1 + 0.9;
        finalColor.a *= scanline;
    } else if (elementType == 1) { // Running text
        float scroll = fract(TexCoord.x + time * 0.1);
        finalColor.rgb *= sin(scroll * 3.14159) * 0.5 + 0.5;
    } else if (elementType == 2) { // Animated bar
        float pulse = sin(time * 3.0) * 0.3 + 0.7;
        finalColor.rgb *= pulse;
    }
    
    FragColor = finalColor;
}
)";

// ============================================================================
// Function Declarations
// ============================================================================
unsigned int loadShader(const char* vertexSource, const char* fragmentSource);
unsigned int createQuad();
unsigned int createCube();
void initScene();
void processInput(GLFWwindow* window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void renderScene(float time, glm::mat4 view, glm::mat4 projection);
void renderBloom();
void renderUI(float time);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// ============================================================================
// Main
// ============================================================================
int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Neon Cyber-City at Night", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    // Load shaders
    sceneShader = loadShader(sceneVertexSource, sceneFragmentSource);
    bloomExtractShader = loadShader(bloomExtractVertexSource, bloomExtractFragmentSource);
    blurShader = loadShader(blurVertexSource, blurFragmentSource);
    bloomCompositeShader = loadShader(bloomCompositeVertexSource, bloomCompositeFragmentSource);
    uiShader = loadShader(uiVertexSource, uiFragmentSource);

    // Create geometry
    quadVAO = createQuad();
    createCube();

    // Initialize scene objects
    initScene();

    // Setup HDR framebuffer
    glGenFramebuffers(1, &hdrFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);

    // Create two color buffers for ping-ponging
    for (int i = 0; i < 2; i++) {
        glGenTextures(1, &hdrColorbuffer[i]);
        glBindTexture(GL_TEXTURE_2D, hdrColorbuffer[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, hdrColorbuffer[i], 0);
    }

    // Create ping-pong FBOs for blur
    for (int i = 0; i < 2; i++) {
        glGenFramebuffers(1, &pingpongFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[i]);
        glGenTextures(1, &pingpongColorbuffer[i]);
        glBindTexture(GL_TEXTURE_2D, pingpongColorbuffer[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingpongColorbuffer[i], 0);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer not complete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD/Arrows - Move camera" << std::endl;
    std::cout << "  Mouse - Look around" << std::endl;
    std::cout << "  Space/C - Toggle camera mode (Fly/Drive)" << std::endl;
    std::cout << "  B - Toggle bloom" << std::endl;
    std::cout << "  U - Toggle UI" << std::endl;
    std::cout << "  Escape - Exit" << std::endl;

    // Render loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        globalTime = currentFrame;

        processInput(window);

        // Render to HDR framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), 
                                                static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 
                                                0.1f, 1000.0f);

        renderScene(globalTime, view, projection);

        // Apply bloom post-processing
        renderBloom();

        // Render UI
        if (showUI) {
            renderUI(globalTime);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteVertexArrays(1, &cityVAO);
    glDeleteBuffers(1, &cityVBO);
    glDeleteVertexArrays(1, &roadVAO);
    glDeleteBuffers(1, &roadVBO);
    glDeleteVertexArrays(1, &carVAO);
    glDeleteBuffers(1, &carVBO);
    glDeleteVertexArrays(1, &droneVAO);
    glDeleteBuffers(1, &droneVBO);
    glDeleteVertexArrays(1, &billboardVAO);
    glDeleteBuffers(1, &billboardVBO);
    glDeleteVertexArrays(1, &steamVAO);
    glDeleteBuffers(1, &steamVBO);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);

    glDeleteFramebuffers(1, &hdrFBO);
    for (int i = 0; i < 2; i++) {
        glDeleteTextures(1, &hdrColorbuffer[i]);
        glDeleteFramebuffers(1, &pingpongFBO[i]);
        glDeleteTextures(1, &pingpongColorbuffer[i]);
    }

    glDeleteProgram(sceneShader);
    glDeleteProgram(bloomExtractShader);
    glDeleteProgram(blurShader);
    glDeleteProgram(bloomCompositeShader);
    glDeleteProgram(uiShader);

    glfwTerminate();
    return 0;
}

// ============================================================================
// Shader Loading
// ============================================================================
unsigned int loadShader(const char* vertexSource, const char* fragmentSource) {
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, nullptr);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex shader compilation failed:\n" << infoLog << std::endl;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment shader compilation failed:\n" << infoLog << std::endl;
    }

    unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// ============================================================================
// Geometry Creation
// ============================================================================
unsigned int createQuad() {
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    unsigned int quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    return quadVAO;
}

unsigned int createCube() {
    float cubeVertices[] = {
        // positions          // normals           // texcoords
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f, 1.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f, 0.0f,

        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f
    };

    unsigned int indices[] = {
        0,  1,  2,  2,  3,  0,
        4,  5,  6,  6,  7,  4,
        8,  9,  10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20
    };

    glGenVertexArrays(1, &cityVAO);
    glGenBuffers(1, &cityVBO);
    glGenElementArrayBuffers(1, &cityEBO);
    glBindVertexArray(cityVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cityVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cityEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    // Road VAO
    glGenVertexArrays(1, &roadVAO);
    glGenBuffers(1, &roadVBO);
    glBindVertexArray(roadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, roadVBO);
    
    float roadVertices[] = {
        // positions          // normals           // texcoords
        -50.0f, 0.0f, -50.0f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
         50.0f, 0.0f, -50.0f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
         50.0f, 0.0f,  50.0f,  0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
        -50.0f, 0.0f,  50.0f,  0.0f,  1.0f,  0.0f,  0.0f, 1.0f
    };
    
    glBufferData(GL_ARRAY_BUFFER, sizeof(roadVertices), roadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
    return cityVAO;
}

// ============================================================================
// Scene Initialization
// ============================================================================
void initScene() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> posDist(-40.0f, 40.0f);
    std::uniform_real_distribution<float> heightDist(5.0f, 30.0f);
    std::uniform_real_distribution<float> widthDist(2.0f, 6.0f);
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

    // Generate buildings
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building b;
        b.position = glm::vec3(posDist(gen), 0.0f, posDist(gen));
        b.size = glm::vec3(widthDist(gen), heightDist(gen), widthDist(gen));
        b.windowColor = glm::vec3(
            0.5f + colorDist(gen) * 0.5f,
            0.5f + colorDist(gen) * 0.5f,
            0.8f + colorDist(gen) * 0.2f
        );
        b.windowBlinkOffset = colorDist(gen) * 6.28f;
        
        // Keep buildings away from center (road area)
        if (abs(b.position.x) < 8.0f) {
            b.position.x += (b.position.x > 0 ? 10.0f : -10.0f);
        }
        
        buildings.push_back(b);
    }

    // Generate flying cars
    for (int i = 0; i < MAX_FLYING_CARS; i++) {
        FlyingCar car;
        car.lane = 3.0f + (i % 3) * 2.0f;
        car.position = glm::vec3(
            (i % 2 == 0 ? -1.0f : 1.0f) * 50.0f,
            car.lane,
            (rand() % 100 - 50) * 0.5f
        );
        car.velocity = glm::vec3(
            (i % 2 == 0 ? 1.0f : -1.0f) * (5.0f + rand() % 5),
            0.0f,
            0.0f
        );
        car.color = glm::vec3(
            0.8f + colorDist(gen) * 0.2f,
            colorDist(gen) * 0.5f,
            colorDist(gen) * 0.2f
        );
        car.blinkOffset = colorDist(gen) * 6.28f;
        flyingCars.push_back(car);
    }

    // Generate drones
    for (int i = 0; i < MAX_DRONES; i++) {
        Drone d;
        d.position = glm::vec3(
            posDist(gen),
            10.0f + colorDist(gen) * 15.0f,
            posDist(gen)
        );
        d.target = glm::vec3(
            posDist(gen),
            10.0f + colorDist(gen) * 15.0f,
            posDist(gen)
        );
        d.speed = 2.0f + colorDist(gen) * 3.0f;
        d.hoverHeight = colorDist(gen) * 0.5f;
        d.blinkOffset = colorDist(gen) * 6.28f;
        drones.push_back(d);
    }

    // Generate steam emitters
    for (int i = 0; i < MAX_STEAM_EMITTERS; i++) {
        SteamEmitter e;
        e.position = glm::vec3(
            (rand() % 20 - 10) * 2.0f,
            0.1f,
            (rand() % 20 - 10) * 2.0f
        );
        e.emissionTimer = colorDist(gen) * 6.28f;
        steamEmitters.push_back(e);
    }

    // Generate neon lights
    for (int i = 0; i < MAX_NEON_LIGHTS; i++) {
        NeonLight light;
        light.position = glm::vec3(
            buildings[i % buildings.size()].position.x,
            buildings[i % buildings.size()].position.y + 
                buildings[i % buildings.size()].size.y * (0.3f + colorDist(gen) * 0.6f),
            buildings[i % buildings.size()].position.z
        );
        light.color = glm::vec3(
            colorDist(gen),
            colorDist(gen),
            0.8f + colorDist(gen) * 0.2f
        );
        light.intensity = 0.5f + colorDist(gen) * 0.5f;
        light.blinkSpeed = 1.0f + colorDist(gen) * 3.0f;
        light.blinkOffset = colorDist(gen) * 6.28f;
        neonLights.push_back(light);
    }

    // Generate billboards
    for (int i = 0; i < MAX_BILLBOARDS; i++) {
        Billboard bb;
        bb.position = glm::vec3(
            buildings[i % buildings.size()].position.x,
            buildings[i % buildings.size()].position.y + 
                buildings[i % buildings.size()].size.y * 0.7f,
            buildings[i % buildings.size()].position.z + 
                (buildings[i % buildings.size()].size.z * 0.6f)
        );
        bb.rotation = colorDist(gen) * 6.28f;
        bb.pattern = rand() % 4;
        bb.animTimer = colorDist(gen) * 6.28f;
        billboards.push_back(bb);
    }
}

// ============================================================================
// Input Handling
// ============================================================================
void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float velocity = moveSpeed * deltaTime;
    
    if (cameraMode == 0) { // Fly mode
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            cameraPos += cameraFront * velocity;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            cameraPos -= cameraFront * velocity;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * velocity;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * velocity;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            cameraPos += cameraUp * velocity;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            cameraPos -= cameraUp * velocity;
    } else { // Drive mode
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            cameraPos += cameraFront * velocity;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            cameraPos -= cameraFront * velocity;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * velocity;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * velocity;
        // Keep camera at street level
        cameraPos.y = 2.0f;
    }

    // Toggle camera mode
    static bool spacePressed = false;
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !spacePressed) {
        cameraMode = 1 - cameraMode;
        spacePressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE) {
        spacePressed = false;
    }

    // Toggle bloom
    static bool bPressed = false;
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS && !bPressed) {
        useBloom = !useBloom;
        bPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_RELEASE) {
        bPressed = false;
    }

    // Toggle UI
    static bool uPressed = false;
    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS && !uPressed) {
        showUI = !showUI;
        uPressed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_RELEASE) {
        uPressed = false;
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    yaw += xoffset * 0.1f;
    pitch += yoffset * 0.1f;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    moveSpeed += yoffset * 0.5f;
    if (moveSpeed < 1.0f) moveSpeed = 1.0f;
    if (moveSpeed > 30.0f) moveSpeed = 30.0f;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// ============================================================================
// Rendering Functions
// ============================================================================
void renderScene(float time, glm::mat4 view, glm::mat4 projection) {
    glUseProgram(sceneShader);

    glUniformMatrix4fv(glGetUniformLocation(sceneShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(sceneShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3f(glGetUniformLocation(sceneShader, "viewPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(sceneShader, "time"), time);

    // Set up lights
    int numLights = std::min((int)neonLights.size(), 30);
    for (int i = 0; i < numLights; i++) {
        float blink = sin(time * neonLights[i].blinkSpeed + neonLights[i].blinkOffset) * 0.5f + 0.5f;
        glUniform3f(glGetUniformLocation(sceneShader, ("lightPositions[" + std::to_string(i) + "]").c_str()),
                    neonLights[i].position.x, neonLights[i].position.y, neonLights[i].position.z);
        glUniform3f(glGetUniformLocation(sceneShader, ("lightColors[" + std::to_string(i) + "]").c_str()),
                    neonLights[i].color.r, neonLights[i].color.g, neonLights[i].color.b);
        glUniform1f(glGetUniformLocation(sceneShader, ("lightIntensities[" + std::to_string(i) + "]").c_str()),
                    neonLights[i].intensity * blink);
    }
    glUniform1i(glGetUniformLocation(sceneShader, "numLights"), numLights);

    // Render buildings
    glBindVertexArray(cityVAO);
    for (const auto& building : buildings) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, building.position);
        model = glm::scale(model, building.size);
        
        glUniformMatrix4fv(glGetUniformLocation(sceneShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(glGetUniformLocation(sceneShader, "objectType"), 0);
        
        // Building base color with neon windows
        float windowBlink = sin(time * 2.0f + building.windowBlinkOffset) * 0.5f + 0.5f;
        glm::vec3 buildingColor = glm::vec3(0.1f, 0.1f, 0.15f);
        if (windowBlink > 0.7f) {
            buildingColor = building.windowColor;
        }
        
        // We would need to pass vertex colors per-instance for proper rendering
        // For simplicity, using uniform color
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    }

    // Render road
    glBindVertexArray(roadVAO);
    glm::mat4 roadModel = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(sceneShader, "model"), 1, GL_FALSE, glm::value_ptr(roadModel));
    glUniform1i(glGetUniformLocation(sceneShader, "objectType"), 4);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Render flying cars
    for (auto& car : flyingCars) {
        car.position += car.velocity * deltaTime;
        if (car.position.x > 60.0f) car.position.x = -60.0f;
        if (car.position.x < -60.0f) car.position.x = 60.0f;

        glm::mat4 carModel = glm::mat4(1.0f);
        carModel = glm::translate(carModel, car.position);
        carModel = glm::scale(carModel, glm::vec3(2.0f, 0.5f, 1.0f));
        
        glUniformMatrix4fv(glGetUniformLocation(sceneShader, "model"), 1, GL_FALSE, glm::value_ptr(carModel));
        glUniform1i(glGetUniformLocation(sceneShader, "objectType"), 1);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    }

    // Render drones
    for (auto& drone : drones) {
        float dist = glm::length(drone.target - drone.position);
        if (dist < 1.0f) {
            drone.target = glm::vec3(
                ((float)(rand() % 100 - 50)) * 0.8f,
                10.0f + ((float)(rand() % 30)) * 0.5f,
                ((float)(rand() % 100 - 50)) * 0.8f
            );
        }
        drone.position += glm::normalize(drone.target - drone.position) * drone.speed * deltaTime;
        drone.position.y += sin(time * 3.0f + drone.blinkOffset) * 0.01f;

        glm::mat4 droneModel = glm::mat4(1.0f);
        droneModel = glm::translate(droneModel, drone.position);
        droneModel = glm::scale(droneModel, glm::vec3(0.5f, 0.2f, 0.5f));
        
        glUniformMatrix4fv(glGetUniformLocation(sceneShader, "model"), 1, GL_FALSE, glm::value_ptr(droneModel));
        glUniform1i(glGetUniformLocation(sceneShader, "objectType"), 2);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    }

    // Render billboards
    for (auto& bb : billboards) {
        bb.animTimer += deltaTime;
        glm::mat4 bbModel = glm::mat4(1.0f);
        bbModel = glm::translate(bbModel, bb.position);
        bbModel = glm::rotate(bbModel, bb.rotation + time * 0.1f, glm::vec3(0.0f, 1.0f, 0.0f));
        bbModel = glm::scale(bbModel, glm::vec3(3.0f, 2.0f, 0.1f));
        
        glUniformMatrix4fv(glGetUniformLocation(sceneShader, "model"), 1, GL_FALSE, glm::value_ptr(bbModel));
        glUniform1i(glGetUniformLocation(sceneShader, "objectType"), 3);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    }

    glBindVertexArray(0);
}

void renderBloom() {
    if (!useBloom) {
        // Just display the scene directly
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        
        glUseProgram(bloomCompositeShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColorbuffer[0]);
        glUniform1i(glGetUniformLocation(bloomCompositeShader, "sceneTexture"), 0);
        
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        return;
    }

    // Extract bright areas
    glUseProgram(bloomExtractShader);
    glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColorbuffer[0]);
    glUniform1i(glGetUniformLocation(bloomExtractShader, "sceneTexture"), 0);
    glUniform1f(glGetUniformLocation(bloomExtractShader, "threshold"), 0.8f);
    
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Blur passes
    glUseProgram(blurShader);
    bool horizontal = true;
    for (int i = 0; i < 5; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingpongFBO[horizontal ? 1 : 0]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, pingpongColorbuffer[horizontal ? 0 : 1]);
        glUniform1i(glGetUniformLocation(blurShader, "image"), 0);
        glUniform1i(glGetUniformLocation(blurShader, "horizontal"), horizontal);
        glUniform2f(glGetUniformLocation(blurShader, "texelSize"), 
                    1.0f / WINDOW_WIDTH, 1.0f / WINDOW_HEIGHT);
        
        glDrawArrays(GL_TRIANGLES, 0, 6);
        horizontal = !horizontal;
    }

    // Composite
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(bloomCompositeShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrColorbuffer[0]);
    glUniform1i(glGetUniformLocation(bloomCompositeShader, "sceneTexture"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pingpongColorbuffer[horizontal ? 0 : 1]);
    glUniform1i(glGetUniformLocation(bloomCompositeShader, "bloomTexture"), 1);
    
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void renderUI(float time) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(uiShader);

    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(WINDOW_WIDTH), 
                                       0.0f, static_cast<float>(WINDOW_HEIGHT));
    glUniformMatrix4fv(glGetUniformLocation(uiShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(uiShader, "time"), time);

    // Draw scanlines overlay
    float scanlineVertices[] = {
        // pos         // texcoord    // color
        0.0f, 0.0f,    0.0f, 0.0f,    0.0f, 1.0f, 0.5f, 0.1f,
        (float)WINDOW_WIDTH, 0.0f,    1.0f, 0.0f,    0.0f, 1.0f, 0.5f, 0.1f,
        (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 1.0f, 1.0f,    0.0f, 1.0f, 0.5f, 0.1f,
        0.0f, (float)WINDOW_HEIGHT, 0.0f, 1.0f,    0.0f, 1.0f, 0.5f, 0.1f
    };

    unsigned int uiVAO, uiVBO;
    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(scanlineVertices), scanlineVertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));

    glUniform1i(glGetUniformLocation(uiShader, "elementType"), 0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Draw futuristic border
    float borderWidth = 5.0f;
    float borderColor[] = {0.0f, 1.0f, 0.8f, 0.8f};
    
    // Top border
    float topBorder[] = {
        0.0f, (float)WINDOW_HEIGHT - borderWidth,    0.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT - borderWidth,    1.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT,    1.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        0.0f, (float)WINDOW_HEIGHT,    0.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3]
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(topBorder), topBorder, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    
    glUniform1i(glGetUniformLocation(uiShader, "elementType"), 2);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Bottom border
    float bottomBorder[] = {
        0.0f, 0.0f,    0.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        (float)WINDOW_WIDTH, 0.0f,    1.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        (float)WINDOW_WIDTH, borderWidth,    1.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        0.0f, borderWidth,    0.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3]
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(bottomBorder), bottomBorder, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Left border
    float leftBorder[] = {
        0.0f, 0.0f,    0.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        borderWidth, 0.0f,    1.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        borderWidth, (float)WINDOW_HEIGHT,    1.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        0.0f, (float)WINDOW_HEIGHT,    0.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3]
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(leftBorder), leftBorder, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Right border
    float rightBorder[] = {
        (float)WINDOW_WIDTH - borderWidth, 0.0f,    0.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        (float)WINDOW_WIDTH, 0.0f,    1.0f, 1.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT,    1.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3],
        (float)WINDOW_WIDTH - borderWidth, (float)WINDOW_HEIGHT,    0.0f, 0.0f,    borderColor[0], borderColor[1], borderColor[2], borderColor[3]
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rightBorder), rightBorder, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Draw running text (simulated with colored rectangles)
    float textY = 30.0f;
    float textHeight = 20.0f;
    float textWidth = 200.0f;
    float textSpeed = time * 50.0f;
    float textX = fmod(textSpeed, (float)WINDOW_WIDTH + textWidth) - textWidth;
    
    float textColor[] = {0.0f, 1.0f, 0.8f, 1.0f};
    float runningText[] = {
        textX, textY,    0.0f, 1.0f,    textColor[0], textColor[1], textColor[2], textColor[3],
        textX + textWidth, textY,    1.0f, 1.0f,    textColor[0], textColor[1], textColor[2], textColor[3],
        textX + textWidth, textY + textHeight,    1.0f, 0.0f,    textColor[0], textColor[1], textColor[2], textColor[3],
        textX, textY + textHeight,    0.0f, 0.0f,    textColor[0], textColor[1], textColor[2], textColor[3]
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(runningText), runningText, GL_STATIC_DRAW);
    glUniform1i(glGetUniformLocation(uiShader, "elementType"), 1);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Draw status indicators
    float indicatorSize = 10.0f;
    float indicatorX = WINDOW_WIDTH - 100.0f;
    float indicatorY = WINDOW_HEIGHT - 50.0f;
    
    // Bloom indicator
    float bloomColor[] = {useBloom ? 0.0f : 1.0f, useBloom ? 1.0f : 0.0f, 0.0f, 1.0f};
    float bloomIndicator[] = {
        indicatorX, indicatorY,    0.0f, 1.0f,    bloomColor[0], bloomColor[1], bloomColor[2], bloomColor[3],
        indicatorX + indicatorSize, indicatorY,    1.0f, 1.0f,    bloomColor[0], bloomColor[1], bloomColor[2], bloomColor[3],
        indicatorX + indicatorSize, indicatorY + indicatorSize,    1.0f, 0.0f,    bloomColor[0], bloomColor[1], bloomColor[2], bloomColor[3],
        indicatorX, indicatorY + indicatorSize,    0.0f, 0.0f,    bloomColor[0], bloomColor[1], bloomColor[2], bloomColor[3]
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(bloomIndicator), bloomIndicator, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // Camera mode indicator
    float camColor[] = {cameraMode == 0 ? 0.0f : 1.0f, cameraMode == 0 ? 1.0f : 0.0f, 0.8f, 1.0f};
    float camIndicator[] = {
        indicatorX + 20.0f, indicatorY,    0.0f, 1.0f,    camColor[0], camColor[1], camColor[2], camColor[3],
        indicatorX + 30.0f, indicatorY,    1.0f, 1.0f,    camColor[0], camColor[1], camColor[2], camColor[3],
        indicatorX + 30.0f, indicatorY + indicatorSize,    1.0f, 0.0f,    camColor[0], camColor[1], camColor[2], camColor[3],
        indicatorX + 20.0f, indicatorY + indicatorSize,    0.0f, 0.0f,    camColor[0], camColor[1], camColor[2], camColor[3]
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(camIndicator), camIndicator, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDeleteVertexArrays(1, &uiVAO);
    glDeleteBuffers(1, &uiVBO);

    glEnable(GL_DEPTH_TEST);
}
