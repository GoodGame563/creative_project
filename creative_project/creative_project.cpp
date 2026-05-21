// ============================================================================
// NEON CYBER-CITY AT NIGHT - OpenGL Project
// ============================================================================
// Высокие небоскрёбы, летающие машины, голограммы, мокрый асфальт с отражениями
// Много цветных неоновых источников света (point + spot)
// Анимированные рекламные щиты, мигающие огни, пар из канализации, пролетающие дроны
// Сильный bloom и glow эффекты
// Камера может летать между зданиями или ехать по улице
// 2D: футуристический интерфейс с бегущей строкой и сканлайнами
// ============================================================================

#include <GL/glew.h>
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

#pragma comment(lib, "glew32.lib")
#pragma comment(lib, "glfw3.lib")
#pragma comment(lib, "opengl32.lib")

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
GLuint fbo, colorTexture, depthRBO, bloomTexture, blurTexture1, blurTexture2;

// Shader programs
GLuint cityShader, lightingShader, bloomShader, blurShader, compositeShader;
GLuint uiShader, steamShader, billboardShader, carShader, droneShader;

// Camera
glm::vec3 cameraPos(0.0f, 10.0f, 50.0f);
glm::vec3 cameraFront(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);
float yaw = -90.0f;
float pitch = 0.0f;
float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Movement state
bool moveForward = false, moveBackward = false, moveLeft = false, moveRight = false;
bool moveUp = false, moveDown = false;
bool flyMode = true;

// Time and animation
float globalTime = 0.0f;

// Random generator
std::mt19937 rng(std::random_device{}());

// ============================================================================
// Structures
// ============================================================================
struct Building {
    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 neonColor;
    float neonIntensity;
    int windowsPattern;
};

struct FlyingCar {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float size;
    float lane;
    bool direction; // true = forward, false = backward
};

struct Drone {
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 color;
    float speed;
    float hoverOffset;
};

struct NeonLight {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;
    int type; // 0 = point, 1 = spot
    glm::vec3 direction;
    float cutoff;
};

struct Billboard {
    glm::vec3 position;
    glm::vec2 scale;
    glm::vec3 baseColor;
    float animationSpeed;
    int pattern;
    float timeOffset;
};

struct SteamEmitter {
    glm::vec3 position;
    float emissionRate;
    float particleLifetime;
};

std::vector<Building> buildings;
std::vector<FlyingCar> flyingCars;
std::vector<Drone> drones;
std::vector<NeonLight> neonLights;
std::vector<Billboard> billboards;
std::vector<SteamEmitter> steamEmitters;

// ============================================================================
// Shader Sources
// ============================================================================
const char* cityVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec3 ViewDir;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    ViewDir = cameraPos - worldPos.xyz;
    gl_Position = projection * view * worldPos;
}
)";

const char* cityFragmentShaderSource = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 ViewDir;

out vec4 FragColor;

uniform vec3 buildingColor;
uniform vec3 neonColor;
uniform float neonIntensity;
uniform int windowsPattern;
uniform float time;
uniform vec3 cameraPos;

void main()
{
    vec3 normal = normalize(Normal);
    vec3 viewDir = normalize(ViewDir);
    
    // Base building color (dark glass/concrete)
    vec3 baseColor = buildingColor * 0.1;
    
    // Procedural windows
    vec2 uv = FragPos.xz * 0.1;
    float windowPattern = 0.0;
    
    if (windowsPattern == 0) {
        // Grid pattern
        float gridX = fract(uv.x * 10.0);
        float gridY = fract(uv.y * 10.0);
        windowPattern = step(0.7, gridX) * step(0.7, gridY);
    } else if (windowsPattern == 1) {
        // Random flickering
        float noise = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
        float flicker = 0.5 + 0.5 * sin(time * 2.0 + noise * 10.0);
        windowPattern = step(0.5, noise) * flicker;
    } else {
        // Horizontal strips
        windowPattern = step(0.8, fract(uv.y * 20.0));
    }
    
    vec3 windowColor = neonColor * windowPattern * neonIntensity;
    
    // Wet surface reflection (Fresnel-like effect)
    float fresnel = pow(1.0 - abs(dot(normal, viewDir)), 3.0);
    vec3 reflection = vec3(0.1, 0.2, 0.3) * fresnel;
    
    FragColor = vec4(baseColor + windowColor + reflection, 1.0);
}
)";

const char* carVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * worldPos;
}
)";

const char* carFragmentShaderSource = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 carColor;
uniform vec3 headlightColor;
uniform vec3 taillightColor;
uniform float isHeadlightVisible;

void main()
{
    vec3 normal = normalize(Normal);
    vec3 viewDir = normalize(cameraPos - FragPos);
    
    // Metallic car body
    vec3 baseColor = carColor;
    float specular = pow(max(dot(normal, viewDir), 0.0), 32.0);
    vec3 metallic = baseColor * 0.5 + vec3(0.3) * specular;
    
    // Headlights (front)
    float headlight = step(0.8, abs(normal.z)) * step(0.0, normal.z) * isHeadlightVisible;
    
    // Taillights (back)
    float taillight = step(0.8, abs(normal.z)) * step(normal.z, 0.0);
    
    FragColor = vec4(metallic + headlight * headlightColor + taillight * taillightColor, 1.0);
}
)";

const char* droneVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * worldPos;
}
)";

const char* droneFragmentShaderSource = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 droneColor;
uniform float blinkState;

void main()
{
    vec3 normal = normalize(Normal);
    vec3 viewDir = normalize(cameraPos - FragPos);
    
    // Dark drone body
    vec3 bodyColor = vec3(0.1);
    
    // Blinking LED
    float led = blinkState * step(0.9, abs(normal.y));
    vec3 ledColor = droneColor * led * 2.0;
    
    // Specular highlights
    float specular = pow(max(dot(normal, viewDir), 0.0), 64.0);
    
    FragColor = vec4(bodyColor + ledColor + vec3(specular * 0.3), 1.0);
}
)";

const char* billboardVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform float timeOffset;
uniform int pattern;
uniform vec3 baseColor;

out vec2 TexCoord;
out vec3 Color;
out float AnimationPhase;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
    AnimationPhase = time + timeOffset;
    Color = baseColor;
    gl_Position = projection * view * worldPos;
}
)";

const char* billboardFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
in vec3 Color;
in float AnimationPhase;

out vec4 FragColor;

void main()
{
    vec3 finalColor = Color;
    
    // Animated patterns
    float t = AnimationPhase;
    
    // Pattern 0: Scrolling text simulation (moving bars)
    float bar = step(0.5, sin(TexCoord.y * 20.0 + t * 3.0));
    
    // Pattern 1: Pulsing circles
    float dist = length(TexCoord - 0.5);
    float circle = 1.0 - smoothstep(0.0, 0.3, abs(dist - 0.2 * sin(t)));
    
    // Pattern 2: Checkerboard flash
    float check = step(0.5, fract(TexCoord.x * 10.0)) * step(0.5, fract(TexCoord.y * 10.0));
    float flash = 0.5 + 0.5 * sin(t * 5.0);
    
    // Pattern 3: Rainbow gradient
    vec3 rainbow = vec3(sin(t + TexCoord.x * 6.28), sin(t + 2.0 + TexCoord.x * 6.28), sin(t + 4.0 + TexCoord.x * 6.28));
    
    vec3 patternColor;
    patternColor = mix(vec3(1.0, 0.2, 0.5), vec3(0.2, 0.5, 1.0), bar);
    patternColor = mix(patternColor, vec3(0.0, 1.0, 0.5) * circle, 0.5);
    patternColor = mix(patternColor, vec3(1.0, 1.0, 0.2) * check * flash, 0.3);
    patternColor = mix(patternColor, rainbow, 0.4);
    
    // Add glow
    float glow = 0.5 + 0.5 * sin(t * 2.0);
    patternColor *= 1.0 + glow * 0.5;
    
    FragColor = vec4(patternColor, 1.0);
}
)";

const char* steamVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform float emitterTime;

out float Alpha;
out float Height;

void main()
{
    float age = mod(time - emitterTime, 3.0);
    float lifetime = 3.0;
    
    // Rise up and expand
    float rise = age / lifetime;
    vec3 pos = aPos;
    pos.y += rise * 5.0;
    pos.x *= 1.0 + rise * 2.0;
    pos.z *= 1.0 + rise * 2.0;
    
    vec4 worldPos = model * vec4(pos, 1.0);
    Height = rise;
    Alpha = 1.0 - rise;
    
    gl_Position = projection * view * worldPos;
}
)";

const char* steamFragmentShaderSource = R"(
#version 330 core
in float Alpha;
in float Height;

out vec4 FragColor;

void main()
{
    // Soft white/gray steam with slight blue tint from neon
    vec3 steamColor = vec3(0.6, 0.7, 0.8) * (0.5 + Height * 0.5);
    float alpha = Alpha * 0.3;
    
    FragColor = vec4(steamColor, alpha);
}
)";

const char* postProcessVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 1.0);
}
)";

const char* bloomExtractShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D sceneTexture;
uniform float threshold;

void main()
{
    vec3 color = texture(sceneTexture, TexCoord).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 brightColor = max(color - threshold, vec3(0.0));
    FragColor = vec4(brightColor, 1.0);
}
)";

const char* blurShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D image;
uniform vec2 direction;
uniform float sigma;

const int kernelSize = 15;
float gaussianKernel[kernelSize];

void computeGaussianKernel(float sigma)
{
    float sum = 0.0;
    for (int i = 0; i < kernelSize; i++) {
        float x = float(i - kernelSize/2);
        gaussianKernel[i] = exp(-(x * x) / (2.0 * sigma * sigma));
        sum += gaussianKernel[i];
    }
    for (int i = 0; i < kernelSize; i++) {
        gaussianKernel[i] /= sum;
    }
}

void main()
{
    computeGaussianKernel(sigma);
    vec3 result = vec3(0.0);
    vec2 texelSize = 1.0 / vec2(textureSize(image, 0));
    
    for (int i = 0; i < kernelSize; i++) {
        vec2 offset = direction * texelSize * float(i - kernelSize/2);
        result += texture(image, TexCoord + offset).rgb * gaussianKernel[i];
    }
    
    FragColor = vec4(result, 1.0);
}
)";

const char* compositeShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D sceneTexture;
uniform sampler2D bloomTexture;

void main()
{
    vec3 sceneColor = texture(sceneTexture, TexCoord).rgb;
    vec3 bloomColor = texture(bloomTexture, TexCoord).rgb;
    
    // Tone mapping and gamma correction
    vec3 hdrColor = sceneColor + bloomColor * 1.5;
    hdrColor = hdrColor / (hdrColor + vec3(1.0)); // Reinhard tone mapping
    hdrColor = pow(hdrColor, vec3(1.0/2.2)); // Gamma correction
    
    FragColor = vec4(hdrColor, 1.0);
}
)";

const char* uiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 projection;

out vec2 TexCoord;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

const char* uiFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform float time;
uniform vec3 textColor;
uniform float scanlineIntensity;

void main()
{
    // Scanlines
    float scanline = sin(TexCoord.y * 800.0 + time * 10.0) * 0.5 + 0.5;
    scanline = 1.0 - scanline * scanlineIntensity * 0.3;
    
    // Futuristic grid background
    float gridX = step(0.95, fract(TexCoord.x * 50.0));
    float gridY = step(0.95, fract(TexCoord.y * 30.0));
    vec3 gridColor = vec3(0.0, 0.3, 0.5) * (gridX + gridY);
    
    // Vignette
    float vignette = 1.0 - length(TexCoord - 0.5) * 0.5;
    
    FragColor = vec4(gridColor * scanline * vignette, 0.5);
}
)";

// ============================================================================
// Function Declarations
// ============================================================================
void processInput(GLFWwindow* window);
void mouseCallback(GLFWwindow* window, double xpos, double ypos);
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

GLuint compileShader(const char* vertexSource, const char* fragmentSource);
GLuint createShaderProgram(const char* vertSrc, const char* fragSrc);

void initCity();
void initFlyingCars();
void initDrones();
void initNeonLights();
void initBillboards();
void initSteamEmitters();
void initBuffers();
void initFramebuffer();

void drawCity();
void drawFlyingCars(float deltaTime);
void drawDrones(float deltaTime);
void drawBillboards();
void drawSteam();
void drawUI();
void applyBloom();

void updateFlyingCars(float deltaTime);
void updateDrones(float deltaTime);

// ============================================================================
// Main Function
// ============================================================================
int main()
{
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
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    // Compile shaders
    cityShader = createShaderProgram(cityVertexShaderSource, cityFragmentShaderSource);
    carShader = createShaderProgram(carVertexShaderSource, carFragmentShaderSource);
    droneShader = createShaderProgram(droneVertexShaderSource, droneFragmentShaderSource);
    billboardShader = createShaderProgram(billboardVertexShaderSource, billboardFragmentShaderSource);
    steamShader = createShaderProgram(steamVertexShaderSource, steamFragmentShaderSource);
    bloomShader = createShaderProgram(postProcessVertexShaderSource, bloomExtractShaderSource);
    blurShader = createShaderProgram(postProcessVertexShaderSource, blurShaderSource);
    compositeShader = createShaderProgram(postProcessVertexShaderSource, compositeShaderSource);
    uiShader = createShaderProgram(uiVertexShaderSource, uiFragmentShaderSource);

    // Initialize scene
    initCity();
    initFlyingCars();
    initDrones();
    initNeonLights();
    initBillboards();
    initSteamEmitters();
    initBuffers();
    initFramebuffer();

    std::cout << "=== NEON CYBER-CITY CONTROLS ===" << std::endl;
    std::cout << "W/S - Move Forward/Backward" << std::endl;
    std::cout << "A/D - Move Left/Right" << std::endl;
    std::cout << "Space/Shift - Move Up/Down" << std::endl;
    std::cout << "Mouse - Look Around" << std::endl;
    std::cout << "F - Toggle Fly/Walk Mode" << std::endl;
    std::cout << "ESC - Exit" << std::endl;
    std::cout << "================================" << std::endl;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        globalTime = currentFrame;

        processInput(window);
        updateFlyingCars(deltaTime);
        updateDrones(deltaTime);

        // Render to framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        drawCity();
        drawFlyingCars(deltaTime);
        drawDrones(deltaTime);
        drawBillboards();
        drawSteam();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        // Apply bloom post-processing
        applyBloom();

        // Draw UI
        drawUI();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    glDeleteVertexArrays(1, &cityVAO);
    glDeleteBuffers(1, &cityVBO);
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

    glDeleteProgram(cityShader);
    glDeleteProgram(carShader);
    glDeleteProgram(droneShader);
    glDeleteProgram(billboardShader);
    glDeleteProgram(steamShader);
    glDeleteProgram(bloomShader);
    glDeleteProgram(blurShader);
    glDeleteProgram(compositeShader);
    glDeleteProgram(uiShader);

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &colorTexture);
    glDeleteTextures(1, &bloomTexture);
    glDeleteTextures(1, &blurTexture1);
    glDeleteTextures(1, &blurTexture2);

    glfwTerminate();
    return 0;
}

// ============================================================================
// Input Handling
// ============================================================================
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    moveForward = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS);
    moveBackward = (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
    moveLeft = (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS);
    moveRight = (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS);
    moveUp = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
    moveDown = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);

    float cameraSpeed = flyMode ? 20.0f : 10.0f;

    if (moveForward)
        cameraPos += cameraFront * cameraSpeed * deltaTime;
    if (moveBackward)
        cameraPos -= cameraFront * cameraSpeed * deltaTime;
    if (moveLeft)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed * deltaTime;
    if (moveRight)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed * deltaTime;
    if (flyMode && moveUp)
        cameraPos += cameraUp * cameraSpeed * deltaTime;
    if (flyMode && moveDown)
        cameraPos -= cameraUp * cameraSpeed * deltaTime;

    // Keep camera above ground in walk mode
    if (!flyMode && cameraPos.y < 2.0f)
        cameraPos.y = 2.0f;
}

void mouseCallback(GLFWwindow* window, double xpos, double ypos)
{
    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    // Can be used for zoom or other effects
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_F && action == GLFW_PRESS)
        flyMode = !flyMode;
}

// ============================================================================
// Shader Compilation
// ============================================================================
GLuint compileShader(const char* vertexSource, const char* fragmentSource)
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, nullptr);
    glCompileShader(vertexShader);

    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex shader compilation error: " << infoLog << std::endl;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment shader compilation error: " << infoLog << std::endl;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Shader program linking error: " << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

GLuint createShaderProgram(const char* vertSrc, const char* fragSrc)
{
    return compileShader(vertSrc, fragSrc);
}

// ============================================================================
// Scene Initialization
// ============================================================================
void initCity()
{
    std::uniform_real_distribution<> posX(-100.0f, 100.0f);
    std::uniform_real_distribution<> posZ(-200.0f, 100.0f);
    std::uniform_real_distribution<> width(8.0f, 15.0f);
    std::uniform_real_distribution<> depth(8.0f, 15.0f);
    std::uniform_real_distribution<> height(30.0f, 120.0f);
    std::uniform_int_distribution<> pattern(0, 2);
    std::uniform_real_distribution<> colorR(0.0f, 1.0f);
    std::uniform_real_distribution<> colorG(0.0f, 1.0f);
    std::uniform_real_distribution<> colorB(0.5f, 1.0f);
    std::uniform_real_distribution<> intensity(0.5f, 1.5f);

    for (int i = 0; i < MAX_BUILDINGS; ++i) {
        Building b;
        b.position = glm::vec3(posX(rng), 0.0f, posZ(rng));
        b.scale = glm::vec3(width(rng), height(rng), depth(rng));
        b.neonColor = glm::vec3(colorR(rng), colorG(rng), colorB(rng));
        b.neonIntensity = intensity(rng);
        b.windowsPattern = pattern(rng);
        buildings.push_back(b);
    }
}

void initFlyingCars()
{
    std::uniform_real_distribution<> posX(-80.0f, 80.0f);
    std::uniform_real_distribution<> posY(5.0f, 80.0f);
    std::uniform_real_distribution<> speed(10.0f, 30.0f);
    std::uniform_real_distribution<> colorR(0.5f, 1.0f);
    std::uniform_real_distribution<> colorG(0.0f, 1.0f);
    std::uniform_real_distribution<> colorB(0.0f, 1.0f);
    std::uniform_real_distribution<> size(1.5f, 3.0f);

    for (int i = 0; i < MAX_FLYING_CARS; ++i) {
        FlyingCar car;
        car.position = glm::vec3(posX(rng), posY(rng), -200.0f + i * 20.0f);
        car.lane = posY(rng);
        car.direction = (i % 2 == 0);
        car.velocity = glm::vec3(0.0f, 0.0f, car.direction ? speed(rng) : -speed(rng));
        car.color = glm::vec3(colorR(rng), colorG(rng), colorB(rng));
        car.size = size(rng);
        flyingCars.push_back(car);
    }
}

void initDrones()
{
    std::uniform_real_distribution<> posX(-90.0f, 90.0f);
    std::uniform_real_distribution<> posY(10.0f, 60.0f);
    std::uniform_real_distribution<> posZ(-180.0f, 80.0f);
    std::uniform_real_distribution<> speed(5.0f, 15.0f);
    std::uniform_real_distribution<> colorR(0.8f, 1.0f);
    std::uniform_real_distribution<> colorG(0.0f, 0.5f);
    std::uniform_real_distribution<> colorB(0.0f, 0.3f);

    for (int i = 0; i < MAX_DRONES; ++i) {
        Drone d;
        d.position = glm::vec3(posX(rng), posY(rng), posZ(rng));
        d.target = glm::vec3(posX(rng), posY(rng), posZ(rng));
        d.speed = speed(rng);
        d.hoverOffset = static_cast<float>(i) * 0.5f;
        d.color = glm::vec3(colorR(rng), colorG(rng), colorB(rng));
        drones.push_back(d);
    }
}

void initNeonLights()
{
    std::uniform_real_distribution<> posX(-95.0f, 95.0f);
    std::uniform_real_distribution<> posY(5.0f, 100.0f);
    std::uniform_real_distribution<> posZ(-190.0f, 90.0f);
    std::uniform_real_distribution<> colorR(0.5f, 1.0f);
    std::uniform_real_distribution<> colorG(0.0f, 1.0f);
    std::uniform_real_distribution<> colorB(0.5f, 1.0f);
    std::uniform_real_distribution<> intensity(0.5f, 2.0f);
    std::uniform_real_distribution<> radius(10.0f, 30.0f);
    std::uniform_int_distribution<> type(0, 1);

    for (int i = 0; i < MAX_NEON_LIGHTS; ++i) {
        NeonLight light;
        light.position = glm::vec3(posX(rng), posY(rng), posZ(rng));
        light.color = glm::vec3(colorR(rng), colorG(rng), colorB(rng));
        light.intensity = intensity(rng);
        light.radius = radius(rng);
        light.type = type(rng);
        light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        light.cutoff = glm::cos(glm::radians(25.0f));
        neonLights.push_back(light);
    }
}

void initBillboards()
{
    std::uniform_real_distribution<> posX(-90.0f, 90.0f);
    std::uniform_real_distribution<> posZ(-180.0f, 80.0f);
    std::uniform_real_distribution<> posY(15.0f, 50.0f);
    std::uniform_real_distribution<> width(8.0f, 15.0f);
    std::uniform_real_distribution<> height(4.0f, 8.0f);
    std::uniform_int_distribution<> pattern(0, 3);
    std::uniform_real_distribution<> speed(0.5f, 3.0f);
    std::uniform_real_distribution<> timeOffset(0.0f, 10.0f);

    for (int i = 0; i < MAX_BILLBOARDS; ++i) {
        Billboard b;
        b.position = glm::vec3(posX(rng), posY(rng), posZ(rng));
        b.scale = glm::vec2(width(rng), height(rng));
        b.baseColor = glm::vec3(1.0f, 1.0f, 1.0f);
        b.pattern = pattern(rng);
        b.animationSpeed = speed(rng);
        b.timeOffset = timeOffset(rng);
        billboards.push_back(b);
    }
}

void initSteamEmitters()
{
    std::uniform_real_distribution<> posX(-90.0f, 90.0f);
    std::uniform_real_distribution<> posZ(-190.0f, 90.0f);

    for (int i = 0; i < MAX_STEAM_EMITTERS; ++i) {
        SteamEmitter e;
        e.position = glm::vec3(posX(rng), 0.5f, posZ(rng));
        e.emissionRate = 0.5f;
        e.particleLifetime = 3.0f;
        steamEmitters.push_back(e);
    }
}

void initBuffers()
{
    // City buildings (instanced cubes)
    float cubeVertices[] = {
        // positions          // normals
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,

        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
    };

    glGenVertexArrays(1, &cityVAO);
    glGenBuffers(1, &cityVBO);
    glGenBuffers(1, &cityInstanceVBO);

    glBindVertexArray(cityVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cityVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Instance data
    std::vector<float> instanceData;
    for (const auto& b : buildings) {
        instanceData.push_back(b.position.x);
        instanceData.push_back(b.position.y);
        instanceData.push_back(b.position.z);
        instanceData.push_back(b.scale.x);
        instanceData.push_back(b.scale.y);
        instanceData.push_back(b.scale.z);
        instanceData.push_back(b.neonColor.r);
        instanceData.push_back(b.neonColor.g);
        instanceData.push_back(b.neonColor.b);
        instanceData.push_back(b.neonIntensity);
        instanceData.push_back(static_cast<float>(b.windowsPattern));
    }

    glBindBuffer(GL_ARRAY_BUFFER, cityInstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(float), instanceData.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(10 * sizeof(float)));
    glEnableVertexAttribArray(6);

    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    glVertexAttribDivisor(4, 1);
    glVertexAttribDivisor(5, 1);
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);

    // Road (wet asphalt)
    float roadVertices[] = {
        // positions          // normals
        -100.0f, 0.0f,  200.0f,  0.0f,  1.0f,  0.0f,
         100.0f, 0.0f,  200.0f,  0.0f,  1.0f,  0.0f,
         100.0f, 0.0f, -250.0f,  0.0f,  1.0f,  0.0f,
         100.0f, 0.0f, -250.0f,  0.0f,  1.0f,  0.0f,
        -100.0f, 0.0f, -250.0f,  0.0f,  1.0f,  0.0f,
        -100.0f, 0.0f,  200.0f,  0.0f,  1.0f,  0.0f
    };

    glGenVertexArrays(1, &roadVAO);
    glGenBuffers(1, &roadVBO);

    glBindVertexArray(roadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, roadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(roadVertices), roadVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Flying car (simple box)
    float carVertices[] = {
        // positions          // normals
        -0.5f, -0.2f, -1.0f,  0.0f,  0.0f, -1.0f,
         0.5f, -0.2f, -1.0f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.2f, -1.0f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.2f, -1.0f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.2f, -1.0f,  0.0f,  0.0f, -1.0f,
        -0.5f, -0.2f, -1.0f,  0.0f,  0.0f, -1.0f,

        -0.5f, -0.2f,  1.0f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.2f,  1.0f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.2f,  1.0f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.2f,  1.0f,  0.0f,  0.0f,  1.0f,
        -0.5f,  0.2f,  1.0f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.2f,  1.0f,  0.0f,  0.0f,  1.0f,

        -0.5f,  0.2f,  1.0f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.2f, -1.0f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.2f, -1.0f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.2f, -1.0f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.2f,  1.0f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.2f,  1.0f, -1.0f,  0.0f,  0.0f,

         0.5f,  0.2f,  1.0f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.2f, -1.0f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.2f, -1.0f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.2f, -1.0f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.2f,  1.0f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.2f,  1.0f,  1.0f,  0.0f,  0.0f,

        -0.5f, -0.2f, -1.0f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.2f, -1.0f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.2f,  1.0f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.2f,  1.0f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.2f,  1.0f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.2f, -1.0f,  0.0f, -1.0f,  0.0f,

        -0.5f,  0.2f, -1.0f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.2f, -1.0f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.2f,  1.0f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.2f,  1.0f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.2f,  1.0f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.2f, -1.0f,  0.0f,  1.0f,  0.0f
    };

    glGenVertexArrays(1, &carVAO);
    glGenBuffers(1, &carVBO);
    glGenBuffers(1, &carInstanceVBO);

    glBindVertexArray(carVAO);
    glBindBuffer(GL_ARRAY_BUFFER, carVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(carVertices), carVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Drone (small cross shape)
    float droneVertices[] = {
        // positions          // normals
        -0.3f, 0.0f, -0.3f,  0.0f,  1.0f,  0.0f,
         0.3f, 0.0f, -0.3f,  0.0f,  1.0f,  0.0f,
         0.3f, 0.0f,  0.3f,  0.0f,  1.0f,  0.0f,
         0.3f, 0.0f,  0.3f,  0.0f,  1.0f,  0.0f,
        -0.3f, 0.0f,  0.3f,  0.0f,  1.0f,  0.0f,
        -0.3f, 0.0f, -0.3f,  0.0f,  1.0f,  0.0f,

        -0.3f, 0.0f, -0.3f,  0.0f, -1.0f,  0.0f,
         0.3f, 0.0f, -0.3f,  0.0f, -1.0f,  0.0f,
         0.3f, 0.0f,  0.3f,  0.0f, -1.0f,  0.0f,
         0.3f, 0.0f,  0.3f,  0.0f, -1.0f,  0.0f,
        -0.3f, 0.0f,  0.3f,  0.0f, -1.0f,  0.0f,
        -0.3f, 0.0f, -0.3f,  0.0f, -1.0f,  0.0f
    };

    glGenVertexArrays(1, &droneVAO);
    glGenBuffers(1, &droneVBO);
    glGenBuffers(1, &droneInstanceVBO);

    glBindVertexArray(droneVAO);
    glBindBuffer(GL_ARRAY_BUFFER, droneVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(droneVertices), droneVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Billboard (quad)
    float billboardVertices[] = {
        // positions          // texcoords
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,   1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,   1.0f, 1.0f,
         0.5f,  0.5f, 0.0f,   1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,   0.0f, 1.0f,
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f
    };

    glGenVertexArrays(1, &billboardVAO);
    glGenBuffers(1, &billboardVBO);
    glGenBuffers(1, &billboardInstanceVBO);

    glBindVertexArray(billboardVAO);
    glBindBuffer(GL_ARRAY_BUFFER, billboardVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(billboardVertices), billboardVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Steam particle (quad)
    float steamVertices[] = {
        // positions
        -0.5f, 0.0f, -0.5f,
         0.5f, 0.0f, -0.5f,
         0.5f, 0.0f,  0.5f,
         0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f, -0.5f
    };

    glGenVertexArrays(1, &steamVAO);
    glGenBuffers(1, &steamVBO);
    glGenBuffers(1, &steamInstanceVBO);

    glBindVertexArray(steamVAO);
    glBindBuffer(GL_ARRAY_BUFFER, steamVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(steamVertices), steamVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    // Screen quad for post-processing
    float quadVertices[] = {
        // positions   // texcoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);

    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void initFramebuffer()
{
    // Create framebuffer for post-processing
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color attachment
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    // Depth buffer
    glGenRenderbuffers(1, &depthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, WINDOW_WIDTH, WINDOW_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Framebuffer not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Bloom textures
    glGenTextures(1, &bloomTexture);
    glBindTexture(GL_TEXTURE_2D, bloomTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &blurTexture1);
    glBindTexture(GL_TEXTURE_2D, blurTexture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(1, &blurTexture2);
    glBindTexture(GL_TEXTURE_2D, blurTexture2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// ============================================================================
// Rendering Functions
// ============================================================================
void drawCity()
{
    glUseProgram(cityShader);

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 0.1f, 500.0f);

    glUniformMatrix4fv(glGetUniformLocation(cityShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(cityShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(cityShader, "cameraPos"), 1, glm::value_ptr(cameraPos));
    glUniform1f(glGetUniformLocation(cityShader, "time"), globalTime);

    // Draw road
    glBindVertexArray(roadVAO);
    glm::mat4 roadModel = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(cityShader, "model"), 1, GL_FALSE, glm::value_ptr(roadModel));
    glUniform3f(glGetUniformLocation(cityShader, "buildingColor"), 0.1f, 0.1f, 0.15f);
    glUniform3f(glGetUniformLocation(cityShader, "neonColor"), 0.0f, 0.0f, 0.0f);
    glUniform1f(glGetUniformLocation(cityShader, "neonIntensity"), 0.0f);
    glUniform1i(glGetUniformLocation(cityShader, "windowsPattern"), 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // Draw buildings (instanced)
    glBindVertexArray(cityVAO);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, static_cast<GLsizei>(buildings.size()));
    glBindVertexArray(0);
}

void updateFlyingCars(float deltaTime)
{
    for (auto& car : flyingCars) {
        car.position += car.velocity * deltaTime;

        // Wrap around
        if (car.position.z > 100.0f)
            car.position.z = -200.0f;
        if (car.position.z < -200.0f)
            car.position.z = 100.0f;
    }
}

void drawFlyingCars(float deltaTime)
{
    glUseProgram(carShader);

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 0.1f, 500.0f);

    glUniformMatrix4fv(glGetUniformLocation(carShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(carShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(carVAO);

    for (const auto& car : flyingCars) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, car.position);
        model = glm::scale(model, glm::vec3(car.size));

        glUniformMatrix4fv(glGetUniformLocation(carShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(glGetUniformLocation(carShader, "carColor"), 1, glm::value_ptr(car.color));
        glUniform3f(glGetUniformLocation(carShader, "headlightColor"), 1.0f, 1.0f, 0.8f);
        glUniform3f(glGetUniformLocation(carShader, "taillightColor"), 1.0f, 0.2f, 0.2f);
        glUniform1f(glGetUniformLocation(carShader, "isHeadlightVisible"), car.velocity.z > 0.0f ? 1.0f : 0.0f);

        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindVertexArray(0);
}

void updateDrones(float deltaTime)
{
    for (auto& drone : drones) {
        // Move towards target
        glm::vec3 direction = drone.target - drone.position;
        float distance = glm::length(direction);

        if (distance < 1.0f) {
            // Pick new random target
            std::uniform_real_distribution<> posX(-90.0f, 90.0f);
            std::uniform_real_distribution<> posY(10.0f, 60.0f);
            std::uniform_real_distribution<> posZ(-180.0f, 80.0f);
            drone.target = glm::vec3(posX(rng), posY(rng), posZ(rng));
        } else {
            drone.position += glm::normalize(direction) * drone.speed * deltaTime;
        }

        // Hover effect
        drone.position.y += sin(globalTime * 3.0f + drone.hoverOffset) * 0.02f;
    }
}

void drawDrones(float deltaTime)
{
    glUseProgram(droneShader);

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 0.1f, 500.0f);

    glUniformMatrix4fv(glGetUniformLocation(droneShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(droneShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(droneVAO);

    float blinkState = 0.5f + 0.5f * sin(globalTime * 10.0f);

    for (const auto& drone : drones) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, drone.position);
        model = glm::rotate(model, globalTime, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(1.5f));

        glUniformMatrix4fv(glGetUniformLocation(droneShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(glGetUniformLocation(droneShader, "droneColor"), 1, glm::value_ptr(drone.color));
        glUniform1f(glGetUniformLocation(droneShader, "blinkState"), blinkState);

        glDrawArrays(GL_TRIANGLES, 0, 12);
    }

    glBindVertexArray(0);
}

void drawBillboards()
{
    glUseProgram(billboardShader);

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 0.1f, 500.0f);

    glUniformMatrix4fv(glGetUniformLocation(billboardShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(billboardShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(billboardShader, "time"), globalTime);

    glBindVertexArray(billboardVAO);

    for (const auto& bb : billboards) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, bb.position);
        model = glm::scale(model, glm::vec3(bb.scale.x, bb.scale.y, 1.0f));

        glUniformMatrix4fv(glGetUniformLocation(billboardShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform1f(glGetUniformLocation(billboardShader, "timeOffset"), bb.timeOffset);
        glUniform1i(glGetUniformLocation(billboardShader, "pattern"), bb.pattern);
        glUniform3fv(glGetUniformLocation(billboardShader, "baseColor"), 1, glm::value_ptr(bb.baseColor));

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
}

void drawSteam()
{
    glUseProgram(steamShader);

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 0.1f, 500.0f);

    glUniformMatrix4fv(glGetUniformLocation(steamShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(steamShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(steamShader, "time"), globalTime);

    glBindVertexArray(steamVAO);

    // Draw simple steam particles at emitter locations
    for (size_t i = 0; i < steamEmitters.size(); ++i) {
        const auto& emitter = steamEmitters[i];
        
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, emitter.position);
        model = glm::scale(model, glm::vec3(2.0f));

        glUniformMatrix4fv(glGetUniformLocation(steamShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform1f(glGetUniformLocation(steamShader, "emitterTime"), static_cast<float>(i));

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
}

void applyBloom()
{
    // Step 1: Extract bright areas
    glUseProgram(bloomShader);
    glBindVertexArray(quadVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glUniform1i(glGetUniformLocation(bloomShader, "sceneTexture"), 0);
    glUniform1f(glGetUniformLocation(bloomShader, "threshold"), 0.8f);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTexture, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Step 2: Horizontal blur
    glUseProgram(blurShader);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurTexture1, 0);
    glUniform1i(glGetUniformLocation(blurShader, "image"), 0);
    glUniform2f(glGetUniformLocation(blurShader, "direction"), 1.0f, 0.0f);
    glUniform1f(glGetUniformLocation(blurShader, "sigma"), 4.0f);
    glBindTexture(GL_TEXTURE_2D, bloomTexture);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Step 3: Vertical blur
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurTexture2, 0);
    glUniform2f(glGetUniformLocation(blurShader, "direction"), 0.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, blurTexture1);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Step 4: Composite
    glUseProgram(compositeShader);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glUniform1i(glGetUniformLocation(compositeShader, "sceneTexture"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, blurTexture2);
    glUniform1i(glGetUniformLocation(compositeShader, "bloomTexture"), 1);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
}

void drawUI()
{
    glDisable(GL_DEPTH_TEST);
    glUseProgram(uiShader);

    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(WINDOW_WIDTH), 0.0f, static_cast<float>(WINDOW_HEIGHT));
    glUniformMatrix4fv(glGetUniformLocation(uiShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1f(glGetUniformLocation(uiShader, "time"), globalTime);
    glUniform3f(glGetUniformLocation(uiShader, "textColor"), 0.0f, 1.0f, 1.0f);
    glUniform1f(glGetUniformLocation(uiShader, "scanlineIntensity"), 0.5f);

    glBindVertexArray(quadVAO);

    // Full-screen overlay with transparency
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

