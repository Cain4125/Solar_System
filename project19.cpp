#define _CRT_SECURE_NO_WARNINGS //--- 프로그램 맨 앞에 선언할 것
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <cmath>

#define M_PI 3.14159265358979323846

//--- 필요한 헤더파일 include
#include <gl/glew.h>
#include <gl/freeglut.h>
#include <gl/freeglut_ext.h>
#include <gl/glm/glm.hpp>
#include <gl/glm/ext.hpp>
#include <gl/glm/gtc/matrix_transform.hpp>
#include <wincodec.h>
#include <GL/glu.h>

// 행성 개수
const int PLANET_COUNT = 8;

// 행성 설정 정보
struct PlanetConfig {
    const char* name;
    float displayRadius; // 구 반지름. 지구 반지름 0.05f 기준 displayRadius = 0.05 * (realRatio ^ 0.4)
    float orbitRadius;   // 궤도 반지름. orbitRadius ≈ 0.6 * sqrt(AU)
	float orbitPeriodDays; // 공전 주기(일). 회전 속도는 Timer에서 변환: 하루당 회전각 = 360 / orbitPeriodDays, 프레임당 회전각 = (360 / orbitPeriodDays) * timeScale. 
                           // 실제 공전 비율을 정확히 유지하되, timeScale로 화면 프레임 속도에 맞게 보정
    float rotationPeriodDays; // 자전 주기(일). 음수면 역자전
    float axialTilt;          // 자전축 기울기(도)
	const char* textureFile;    // 텍스처 파일 경로
};


// 실제 비율을 압축한 값들 (행성 이름, 반지름, 공전 궤도, 공전 주기(속도), 자전 주기, 자전축(기울기정도) 텍스처 파일)
PlanetConfig gPlanets[PLANET_COUNT] = {
    { "Mercury", 0.034f, 0.375f,   87.97f,  58.646f,   0.034f,  "texture/mercury.jpg" },
    { "Venus",   0.049f, 0.509f,  224.70f, -243.025f, 177.36f,  "texture/venus.jpg"   },
    { "Earth",   0.050f, 0.600f,  365.26f,  0.99726968f, 23.44f,  "texture/earth_day.jpg"},
    { "Mars",    0.039f, 0.740f,  686.98f,  1.025957f,  25.19f,  "texture/mars.jpg"    },
    { "Jupiter", 0.130f, 1.368f, 4332.59f,  0.41354f,    3.13f,   "texture/jupiter.jpg" },
    { "Saturn",  0.121f, 1.849f,10759.22f,  0.44401f,   26.73f,   "texture/saturn.jpg"  },
    { "Uranus",  0.087f, 2.629f,30685.40f, -0.71833f,   97.77f,   "texture/uranus.jpg"  },
    { "Neptune", 0.086f, 3.286f,60189.00f,  0.67125f,   28.32f,   "texture/neptune.jpg" }
};

// 태양 크기 (화면 기준)
const float SUN_RADIUS = 0.327f;


// 전역 텍스처 핸들 (0: Sun, 1..N: planets)
GLuint gSunTexture = 0;
GLuint gPlanetTextures[PLANET_COUNT] = { 0 };

// Saturn ring 텍스처
GLuint gSaturnRingTexture = 0;

// 팝업 텍스처 & 마우스 상태
GLuint gPopupTextures[PLANET_COUNT + 1] = { 0 }; // 0..PLANET_COUNT-1: 행성, PLANET_COUNT: 태양
int gHoveredPlanet = -1; // -1: none, PLANET_COUNT = Sun
int gMouseX =0, gMouseY =0;

static GLuint LoadTextureWIC(const char* path);

class Shape {
public:
    std::vector<float> vertices;
    std::vector<float> colors;
    std::vector<int> index; // unsigned int → int로 변경 (18.cpp와 일치)
    float center[3]{};
    float size = 0.5f;
    GLuint VAO, VBO[2], EBO;
    GLUquadricObj* obj = nullptr;
    int type = 0; // 0: orbit, 1: sphere, 2: cylinder, 3: cone

    // 변환 관련 변수들
    int x_rotate = 0, y_rotate = 0, revolution = 0;
    int origin_scale = 0, self_scale = 0;
    float translation[3] = { 0.0f };
    float x_rotationAngle = { 0.0f };
    float y_rotationAngle = { 0.0f };
    float revolutionAngle = { 0.0f };
    float origin_scale_value[3]{ 1.0f, 1.0f, 1.0f };
    float self_scale_value[3]{ 1.0f, 1.0f, 1.0f };

    // 궤도 생성 함수
    void createOrbit(float radius, int segments) {
        vertices.clear();
        index.clear();
        colors.clear();
        type = 0;

        // 원 형태의 정점 생성
        for (int i = 0; i <= segments; i++) {
            float angle = 2.0f * M_PI * i / segments;
            float x = radius * cos(angle);
            float z = radius * sin(angle);

            vertices.push_back(x);
            vertices.push_back(0.0f);
            vertices.push_back(z);

            // 궤도 색상 (회색)
            colors.push_back(0.5f);
            colors.push_back(0.5f);
            colors.push_back(0.5f);

            index.push_back(i);
        }
    }

    // GLU 구체 생성
    void createSphere(float radius = 0.5f) {
        obj = gluNewQuadric();
        gluQuadricDrawStyle(obj, GLU_LINE);
        gluQuadricNormals(obj, GLU_SMOOTH);
        gluQuadricTexture(obj, GL_FALSE);
        size = radius;
        type = 1;
    }

    ~Shape() {
        if (obj) {
            gluDeleteQuadric(obj);
        }
    }
};

//--- 전역 변수들
Shape centerSphere;                  // 태양
Shape planetSpheres[PLANET_COUNT];   // 각 행성
Shape orbits[PLANET_COUNT];          // 각 행성 궤도 

glm::mat4 Matrix[3];
glm::mat4 s_Matrix[3];
glm::mat4 smat[3];
glm::mat4 big_Matrix;
glm::mat4 scalemat(1.0f);
glm::mat4 zmat(1.0f);
glm::mat4 movemat(1.0f);
glm::mat4 gPlanetMatrix[PLANET_COUNT]; // 각 행성의 공전 행렬
float gRevolutionSpeed[PLANET_COUNT]; // 프레임당 회전각도
float gTimeScale = 0.05f;             // 전체 시간 배속에 활용할 변수
float gCameraZ = -4.0f;                // 카메라 z 위치

float gSelfRotationSpeed[PLANET_COUNT] = { 0.0f }; // 프레임당 자전 속도
float gSelfAngle[PLANET_COUNT] = { 0.0f };         // 누적 자전 각도

// 자전 전용 스케일
float gSelfScale = 0.02f;

// 태양 자전 관련 (평균값 사용)
float gSunRotationPeriodDays = 25.38f; // 태양 평균 자전 주기(일)
float gSunSelfSpeed = 0.0f;            // 프레임당 도 (계산해서 사용)
float gSunAngle = 0.0f;                // 누적 자전 각도(도)
float gSunAxialTilt = 7.25f;           // 태양 축 기울기 (도)

bool solid = true, angle = false, z_rotate = false;
bool isGlobalAnimating = false;
bool gPaused = false; // 추가: 't'로 장면 일시정지 토글
float scale =1.0f, zangle =1.0f;
int currentAnimationType =0;

GLint width = 500, height = 500; // 18.cpp와 동일하게
GLuint shaderProgramID;
GLuint vertexShader;
GLuint fragmentShader;

// 달 관련
Shape  moonSphere;
GLuint gMoonTexture = 0;

float  gMoonOrbitRadius = 0.10f;    
float  gMoonRadius = 0.015f;   
float  gMoonOrbitSpeed = 0.0f;    
float  gMoonSelfSpeed = 0.0f;     
float  gMoonOrbitAngle = 0.0f;     
float  gMoonSelfAngle = 0.0f;   

// 위성카메라
int   gFollowPlanet = -1;         // 행성 선택
float gEarthCamOffsetY = 0.15f;   // 위로 띄우는 높이
float gEarthCamOffsetZ = 0.6f;    // 행성 중심에서 떨어진 거리

// === 일식 전용 ===
bool   gEclipseMode = false;   
GLuint gEclipseProg = 0;      
GLuint gEclipseVAO = 0;      
GLuint gEclipseVBO = 0;

// 함수 선언
void make_vertexShaders();
void make_fragmentShaders();
void make_shaderProgram();
GLvoid drawScene();
GLvoid Reshape(int w, int h);
GLvoid Keyboard(unsigned char key, int x, int y);
void TimerFunction(int value);
void CreateMatrix();
void menu();
void scaling(float s);
void updateAnimations();
void startOriginPassAnimation();
void startUpDownAnimation();
GLvoid initBuffer(Shape& shape);
void createAxis(Shape& shape);
void initPlanets();
void updateRevolutionSpeed();
void make_eclipseProgram();
void initEclipseQuad();
void drawEclipseFullscreen();

// --- 마우스 / 팝업 관련 함수 프로토타입 ---
static glm::vec3 projectWorldToScreen(const glm::vec3& worldPos);
void onMouseMove(int x, int y);

char* filetobuf(const char* file) {
    FILE* fptr;
    long length;
    char* buf;
    fptr = fopen(file, "rb");
    if (!fptr)
        return NULL;
    fseek(fptr, 0, SEEK_END);
    length = ftell(fptr);
    buf = (char*)malloc(length + 1);
    fseek(fptr, 0, SEEK_SET);
    fread(buf, length, 1, fptr);
    fclose(fptr);
    buf[length] = 0;
    return buf;
}

void createAxis(Shape& shape) {
    shape.vertices = {
        1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f
    };
    shape.colors = {
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f
    };
    shape.index = {
        0, 1,
        2, 3,
        4, 5
    };
    initBuffer(shape);
}

GLvoid initBuffer(Shape& shape) {
    glGenVertexArrays(1, &shape.VAO);
    glBindVertexArray(shape.VAO);
    glGenBuffers(2, shape.VBO);
    glGenBuffers(1, &shape.EBO);

    glBindBuffer(GL_ARRAY_BUFFER, shape.VBO[0]);
    glBufferData(GL_ARRAY_BUFFER, shape.vertices.size() * sizeof(float), shape.vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, shape.VBO[1]);
    glBufferData(GL_ARRAY_BUFFER, shape.colors.size() * sizeof(float), shape.colors.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);

    if (!shape.index.empty()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, shape.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, shape.index.size() * sizeof(int), shape.index.data(), GL_STATIC_DRAW);
    }
}

void initPlanets() {
    // 태양
    // 태양 텍스처 매핑
    gSunTexture = LoadTextureWIC("texture/sun.jpg");

    //태양 그리기
    centerSphere.createSphere(SUN_RADIUS);
    if (centerSphere.obj) {
        gluQuadricTexture(centerSphere.obj, GL_TRUE);
        gluQuadricNormals(centerSphere.obj, GLU_SMOOTH);
    }



    // 행성
    // 행성 텍스처 매핑
    for (int i = 0; i < PLANET_COUNT; ++i) {
        if (gPlanets[i].textureFile && gPlanets[i].textureFile[0] != '\0') {
            gPlanetTextures[i] = LoadTextureWIC(gPlanets[i].textureFile);
        }
    }

    // Saturn 링 텍스처 로드
    gSaturnRingTexture = LoadTextureWIC("texture/saturn_ring.png");
   
    //행성 그리기
    for (int i = 0; i < PLANET_COUNT; i++) {
        // 궤도 생성
        orbits[i].createOrbit(gPlanets[i].orbitRadius, 120);
        initBuffer(orbits[i]);

        // 구체 생성 (quadric 생성 후 텍스처 좌표 활성화)
        planetSpheres[i].createSphere(gPlanets[i].displayRadius);
        if (planetSpheres[i].obj) {
            gluQuadricTexture(planetSpheres[i].obj, GL_TRUE);   // 중요: 텍스처 좌표 생성 허용
            gluQuadricNormals(planetSpheres[i].obj, GLU_SMOOTH);
            gluQuadricDrawStyle(planetSpheres[i].obj, solid ? GLU_FILL : GLU_LINE);
        }

        // 공전 행렬 배열
        gPlanetMatrix[i] = glm::mat4(1.0f);
    }
    gMoonTexture = LoadTextureWIC("texture/moon.jpg");

    moonSphere.createSphere(gMoonRadius);
    if (moonSphere.obj) {
        gluQuadricTexture(moonSphere.obj, GL_TRUE);
        gluQuadricNormals(moonSphere.obj, GLU_SMOOTH);
        gluQuadricDrawStyle(moonSphere.obj, solid ? GLU_FILL : GLU_LINE);
    }

    // Popup 텍스처 로드: Popup/NAME.png (NAME 대문자)
    for (int i =0; i < PLANET_COUNT; ++i) {
        std::string name(gPlanets[i].name);
        for (auto &c : name) c = (char)toupper(c);
        std::string path = std::string("Popup/") + name + ".png";
        gPopupTextures[i] = LoadTextureWIC(path.c_str());
        if (!gPopupTextures[i]) {
            std::cerr << "Popup texture not found: " << path << std::endl;
}
    }
    // 태양 팝업
    gPopupTextures[PLANET_COUNT] = LoadTextureWIC("Popup/SUN.png");
}

//--- 메인 함수
void main(int argc, char** argv) {
    width = 500;
    height = 500;
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowPosition(100, 100);
    glutInitWindowSize(width, height);
    glutCreateWindow("Solar System Simulater");
    glutFullScreen();

    glewExperimental = GL_TRUE;
    glewInit();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // 조명 시스템 기본 설정
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);        // 스케일,회전해도 normal 자동 보정
    glShadeModel(GL_SMOOTH);

    // glColor로 diffuse/ambient를 같이 조정할 거면
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    // 태양광 색상
    GLfloat lightDiffuse[] = { 1.0f, 1.0f, 0.9f, 1.0f };
    GLfloat lightAmbient[] = { 0.12f, 0.12f, 0.12f, 1.0f };
    GLfloat lightSpecular[] = { 1.0f, 1.0f, 0.9f, 1.0f };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpecular);

    // 거리 감쇠 설정
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);   // 기본 밝기
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.03f);  // 거리 1당 감소
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.05f);  // 거리^2에 비례 감소

    make_shaderProgram();
     

    menu();
    CreateMatrix();

    // COM 초기화 (WIC 사용을 위해)
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    initPlanets(); // 행성 객체 생성

    // 공전 초기화
    updateRevolutionSpeed();


    glutTimerFunc(16, TimerFunction, 0);
    glutDisplayFunc(drawScene);
    glutReshapeFunc(Reshape);
    glutKeyboardFunc(Keyboard);
    glutPassiveMotionFunc(onMouseMove); // 마우스 이동(hover) 처리
    glutMainLoop();
}

// --- 월드->스크린 유틸 ---
static glm::vec3 projectWorldToScreen(const glm::vec3& worldPos) {
 glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f,0.0f, gCameraZ));
 glm::mat4 baseRotation = glm::mat4(1.0f);
 baseRotation = glm::rotate(baseRotation, glm::radians(30.0f), glm::vec3(1.0f,0.0f,0.0f));
 baseRotation = glm::rotate(baseRotation, glm::radians(50.0f), glm::vec3(0.0f, -1.0f,0.0f));
 view = view * baseRotation;

 glm::mat4 proj;
 if (angle) {
 proj = glm::ortho(-2.0f,2.0f, -2.0f,2.0f, -10.0f,10.0f);
 } else {
 proj = glm::perspective(glm::radians(45.0f), (float)width / (float)height,0.1f,100.0f);
 }

 glm::vec4 viewport(0.0f,0.0f, (float)width, (float)height);
 glm::vec3 win = glm::project(worldPos, view, proj, viewport);
 return win; // origin = bottom-left
}

// --- 마우스 이동 콜백 ---
void onMouseMove(int x, int y) {
 gMouseX = x; gMouseY = y;
 if (gFollowPlanet >= 0) {
     gHoveredPlanet = -1;  // 위성 카메라 작동시 무시
     return;
 }
 int picked = -1;
 int mouseYGL = height - y; // convert to OpenGL bottom-left origin

 // Sun
 {
 glm::vec4 sunWorld = glm::vec4(0.0f,0.0f,0.0f,1.0f);
 glm::mat4 transform = movemat * big_Matrix;
 glm::vec3 sunPos = glm::vec3(transform * sunWorld);
 glm::vec3 screen = projectWorldToScreen(sunPos);
 glm::vec3 offset = projectWorldToScreen(sunPos + glm::vec3(centerSphere.size,0.0f,0.0f));
 float pixelRadius = glm::distance(glm::vec2(screen.x, screen.y), glm::vec2(offset.x, offset.y));
 if (pixelRadius <2.0f) pixelRadius = centerSphere.size *100.0f / (fabs(gCameraZ) +1.0f);
 float dx = (float)x - screen.x;
 float dy = (float)mouseYGL - screen.y;
 if (sqrtf(dx*dx + dy*dy) <= pixelRadius +4.0f) picked = PLANET_COUNT;
 }

 if (picked == -1) {
 for (int i =0; i < PLANET_COUNT; ++i) {
 float r = gPlanets[i].orbitRadius;
 glm::vec4 world4 = gPlanetMatrix[i] * glm::vec4(r,0.0f,0.0f,1.0f);
 glm::vec3 worldPos = glm::vec3(world4);
 glm::vec3 screen = projectWorldToScreen(worldPos);
 glm::vec4 off4 = gPlanetMatrix[i] * glm::vec4(r + planetSpheres[i].size,0.0f,0.0f,1.0f);
 glm::vec3 offScr = projectWorldToScreen(glm::vec3(off4));
 float pixelRadius = glm::distance(glm::vec2(screen.x, screen.y), glm::vec2(offScr.x, offScr.y));
 if (pixelRadius <2.0f) pixelRadius = planetSpheres[i].size *100.0f / (fabs(gCameraZ) +1.0f);
 float dx = (float)x - screen.x;
 float dy = (float)mouseYGL - screen.y;
 if (sqrtf(dx*dx + dy*dy) <= pixelRadius +4.0f) { picked = i; break; }
    }
}

 gHoveredPlanet = picked;
 glutPostRedisplay();
}

//--- 셰이더 함수들
void make_vertexShaders() {
    GLchar* vertexSource = filetobuf("vertex_3d.glsl");
    if (!vertexSource) {
        std::cerr << "ERROR: vertex shader 파일을 읽을 수 없습니다." << std::endl;
        return;
    }

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, NULL);
    glCompileShader(vertexShader);

    GLint result;
    GLchar errorLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &result);
    if (!result) {
        glGetShaderInfoLog(vertexShader, 512, NULL, errorLog);
        std::cerr << "ERROR: vertex shader 컴파일 실패\n" << errorLog << std::endl;
    }
    free(vertexSource);
}

void make_fragmentShaders() {
    GLchar* fragmentSource = filetobuf("fragment_3d.glsl");
    if (!fragmentSource) {
        std::cerr << "ERROR: fragment shader 파일을 읽을 수 없습니다." << std::endl;
        return;
    }

    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
    glCompileShader(fragmentShader);

    GLint result;
    GLchar errorLog[512];
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &result);
    if (!result) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, errorLog);
        std::cerr << "ERROR: fragment shader 컴파일 실패\n" << errorLog << std::endl;
    }
    free(fragmentSource);
}

// WIC 기반 텍스처 로더
static GLuint LoadTextureWIC(const char* path) {
    if (!path || !path[0]) return 0;
    IWICImagingFactory* pFactory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pFactory)))) return 0;

    int len = (int)strlen(path);
    std::wstring wpath;
    wpath.resize(len + 1);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], len + 1);

    IWICBitmapDecoder* pDecoder = nullptr;
    if (FAILED(pFactory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &pDecoder))) {
        pFactory->Release();
        return 0;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    if (FAILED(pDecoder->GetFrame(0, &pFrame))) {
        pDecoder->Release();
        pFactory->Release();
        return 0;
    }

    IWICFormatConverter* pConverter = nullptr;
    if (FAILED(pFactory->CreateFormatConverter(&pConverter))) {
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return 0;
    }

    if (FAILED(pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return 0;
    }

    UINT w = 0, h = 0;
    pConverter->GetSize(&w, &h);
    std::vector<BYTE> buf(w * h * 4);
    if (FAILED(pConverter->CopyPixels(nullptr, w * 4, (UINT)buf.size(), buf.data()))) {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_BGRA, GL_UNSIGNED_BYTE, buf.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();
    return tex;
}

void make_eclipseProgram()
{
    char* vsSrc = filetobuf("vertex_eclipse.glsl");
    char* fsSrc = filetobuf("fragment_eclipse.glsl");
    if (!vsSrc || !fsSrc) {
        std::cerr << "일식 셰이더 파일을 읽을 수 없습니다.\n";
        return;
    }

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSrc, NULL);
    glCompileShader(vs);
    free(vsSrc);

    GLint ok; GLchar logBuf[512];
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(vs, 512, NULL, logBuf);
        std::cerr << "vertex_eclipse 컴파일 실패:\n" << logBuf << std::endl;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSrc, NULL);
    glCompileShader(fs);
    free(fsSrc);

    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(fs, 512, NULL, logBuf);
        std::cerr << "fragment_eclipse 컴파일 실패:\n" << logBuf << std::endl;
    }

    gEclipseProg = glCreateProgram();
    glAttachShader(gEclipseProg, vs);
    glAttachShader(gEclipseProg, fs);
    glLinkProgram(gEclipseProg);

    glDeleteShader(vs);
    glDeleteShader(fs);

    glGetProgramiv(gEclipseProg, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(gEclipseProg, 512, NULL, logBuf);
        std::cerr << "일식 프로그램 링크 실패:\n" << logBuf << std::endl;
        gEclipseProg = 0;
    }
}

void make_shaderProgram() {
    make_vertexShaders();
    make_fragmentShaders();
    shaderProgramID = glCreateProgram();
    glAttachShader(shaderProgramID, vertexShader);
    glAttachShader(shaderProgramID, fragmentShader);
    glLinkProgram(shaderProgramID);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint result;
    GLchar errorLog[512];
    glGetProgramiv(shaderProgramID, GL_LINK_STATUS, &result);
    if (!result) {
        glGetProgramInfoLog(shaderProgramID, 512, NULL, errorLog);
        std::cerr << "ERROR: shader program 연결 실패\n" << errorLog << std::endl;
        return;
    }

    glUseProgram(shaderProgramID);
    std::cout << "셰이더 프로그램 초기화 완료" << std::endl;
     
    make_eclipseProgram();
    initEclipseQuad();
}



void initEclipseQuad()
{
    // pos(x,y), uv(u,v)
    float verts[] = {
        //  aPos      aUV
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };

    glGenVertexArrays(1, &gEclipseVAO);
    glGenBuffers(1, &gEclipseVBO);

    glBindVertexArray(gEclipseVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gEclipseVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // aPos (location 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // aUV (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}



//--- 출력 콜백 함수 (18.cpp 스타일로 단순화)
GLvoid drawScene() {
    if (gEclipseMode && gEclipseProg != 0) { 
        glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawEclipseFullscreen();
        glutSwapBuffers();
        return;
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shaderProgramID);

    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    // e 키를 위한 setup
    view = glm::translate(view, glm::vec3(0.0f, 0.0f, gCameraZ));
    if (angle) {
        projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, -10.0f, 10.0f);
    }
    else {
        projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
    }


    glm::mat4 baseRotation = glm::mat4(1.0f);
    baseRotation = glm::rotate(baseRotation, glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    baseRotation = glm::rotate(baseRotation, glm::radians(50.0f), glm::vec3(0.0f, -1.0f, 0.0f));

    unsigned int modelLocation = glGetUniformLocation(shaderProgramID, "Matrix");

    // 모델뷰(카메라) 계산  
    glm::mat4 viewMat;

    if (gFollowPlanet >= 0) {
        int P = gFollowPlanet;
        if (P < 0 || P >= PLANET_COUNT) P = 2; 

        float rP = gPlanets[P].orbitRadius;

        // 행성 중심 월드 좌표 (공전 + 전체 변환)
        glm::mat4 planetBase = movemat * big_Matrix * gPlanetMatrix[P];
        glm::vec3 planetCenterWorld =
            glm::vec3(planetBase * glm::vec4(rP, 0.0f, 0.0f, 1.0f));

        // 태양 월드 좌표
        glm::vec3 sunWorld =
            glm::vec3(movemat * big_Matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        // 행성 → 태양 방향
        glm::vec3 PS = sunWorld - planetCenterWorld;
        glm::vec2 PSxz(PS.x, PS.z);

        float camYawRad = 0.0f;
        if (glm::length(PSxz) >= 1e-6f) { 
            float sunAngle = std::atan2(PSxz.y, PSxz.x); 
            camYawRad = sunAngle - glm::radians(90.0f);
        }

        float camRadius = gEarthCamOffsetZ;
        float cosT = std::cos(camYawRad);
        float sinT = std::sin(camYawRad);

        // 위성 궤도상의 카메라 위치
        glm::vec3 camOffset(
            camRadius * cosT,
            gEarthCamOffsetY,
            camRadius * sinT
        );

        glm::vec3 eyeWorld = planetCenterWorld + camOffset; // 카메라 위치
        glm::vec3 centerWorld = planetCenterWorld;             // 항상 행성 중심을 봄
        glm::vec3 upWorld(0.0f, 1.0f, 0.0f);                  

        viewMat = glm::lookAt(eyeWorld, centerWorld, upWorld);
    }
    else { 
        // 글로벌 카메라
        viewMat = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f, 0.0f, gCameraZ));
    }
    

    // 궤도 그리기
    for (int i = 0; i < PLANET_COUNT; i++) {
        glm::mat4 orbitMatrix;
        if (gFollowPlanet >= 0)
            orbitMatrix = projection * viewMat;
        else
            orbitMatrix = projection * viewMat * baseRotation;
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(orbitMatrix));
        glBindVertexArray(orbits[i].VAO);
        glDrawElements(GL_LINE_LOOP, orbits[i].index.size(), GL_UNSIGNED_INT, 0);
    }

    // --- switch to fixed-function for GLU drawing ---
    glUseProgram(0);

    // 투영 설정
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (angle) {
        glOrtho(-2.0, 2.0, -2.0, 2.0, -10.0, 10.0);
    }
    else {
        gluPerspective(45.0, (double)width / (double)height, 0.1, 100.0);
    }

    // 모델뷰(Fixed-function) 설정: apply same view as used by shader
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (gFollowPlanet >= 0) {
        // load view matrix computed by glm (convert to column-major)
        glLoadMatrixf(glm::value_ptr(viewMat));
    }
    else {
        // global camera: translate + baseRotation
        glTranslatef(0.0f, 0.0f, gCameraZ);
        glMultMatrixf(glm::value_ptr(baseRotation));
    }

    // 태양 위치를 광원으로 설정
    glPushMatrix();
    glMultMatrixf(glm::value_ptr(movemat * big_Matrix));
    GLfloat lightPos[] = { 0.0f,0.0f,0.0f,1.0f }; // 점광원
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

    glPopMatrix();

    // 스타일 설정
    if (solid) {
        if (centerSphere.obj) gluQuadricDrawStyle(centerSphere.obj, GLU_FILL);
        for (int i = 0; i < PLANET_COUNT; i++) {
            if (planetSpheres[i].obj) gluQuadricDrawStyle(planetSpheres[i].obj, GLU_FILL);
        }
    }
    else {
        if (centerSphere.obj) gluQuadricDrawStyle(centerSphere.obj, GLU_LINE);
        for (int i = 0; i < PLANET_COUNT; i++) {
            if (planetSpheres[i].obj) gluQuadricDrawStyle(planetSpheres[i].obj, GLU_LINE);
        }
    }

    // 중심 구 렌더링 - 태양 (텍스처)
    glPushMatrix();
    glMultMatrixf(glm::value_ptr(movemat * big_Matrix));

    // 자체발광 (노란색)
    GLfloat sunEmission[] = { 1.0f, 0.9f, 0.4f, 1.0f };
    GLfloat noEmission[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    glMaterialfv(GL_FRONT, GL_EMISSION, sunEmission);
    glColor3f(1.0f, 1.0f, 1.0f);

    // 태양 축 기울기
    glRotatef(gSunAxialTilt, 0.0f, 0.0f, 1.0f); // 축 기울기
    glRotatef(gSunAngle, 0.0f, 1.0f, 0.0f);     // 자전

    if (solid) {
        if (centerSphere.obj) gluQuadricDrawStyle(centerSphere.obj, GLU_FILL);
        glEnable(GL_TEXTURE_2D);
        if (gSunTexture) glBindTexture(GL_TEXTURE_2D, gSunTexture);
    }
    else {
        if (centerSphere.obj) gluQuadricDrawStyle(centerSphere.obj, GLU_LINE);
        glDisable(GL_TEXTURE_2D);
    }
    if (centerSphere.obj) gluSphere(centerSphere.obj, centerSphere.size, 40, 40);
    if (solid) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    // 자신발광이 다른 객체들에게 영향 안끼치게
    glMaterialfv(GL_FRONT, GL_EMISSION, noEmission);
    glPopMatrix();

    // 행성들
    for (int i = 0; i < PLANET_COUNT; i++) {
        float r = gPlanets[i].orbitRadius;
        glPushMatrix();

        // 공전 행렬 적용
        glMultMatrixf(glm::value_ptr(gPlanetMatrix[i]));

        glTranslatef(r, 0.0f, 0.0f);



        // 축선 그리기 
        glPushMatrix();
        glRotatef(gPlanets[i].axialTilt, 0.0f, 0.0f, 1.0f); // Z축 회전으로 Y축(자전축)을 기울임
        float axisLen = planetSpheres[i].size * 1.5f;
        glDisable(GL_TEXTURE_2D);
        glLineWidth(2.0f);
        glColor3f(0.5f, 0.5f, 0.5f); // 회색 축선
        glBegin(GL_LINES);
        glVertex3f(0.0f, -axisLen, 0.0f);
        glVertex3f(0.0f, axisLen, 0.0f);
        glEnd();
        // 복원 색상
        glColor3f(1.0f, 1.0f, 1.0f);
        glPopMatrix();

        //자전 적용
        glPushMatrix();
        glRotatef(gPlanets[i].axialTilt, 0.0f, 0.0f, 1.0f);      // 자전축 기울기
        glRotatef(gSelfAngle[i], 0.0f, 1.0f, 0.0f);      // 기울어진 축 기준 회전
        glRotatef(90.0f, 1.0f, 0.0f, 0.0f);

        //행성 렌더
        if (solid) {
            if (planetSpheres[i].obj) gluQuadricDrawStyle(planetSpheres[i].obj, GLU_FILL);
            if (gPlanetTextures[i]) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, gPlanetTextures[i]);
            }
            else {
                glDisable(GL_TEXTURE_2D);
            }
        }
        else {
            if (planetSpheres[i].obj) gluQuadricDrawStyle(planetSpheres[i].obj, GLU_LINE);
            glDisable(GL_TEXTURE_2D);
        }

        if (planetSpheres[i].obj)
            gluSphere(planetSpheres[i].obj, planetSpheres[i].size, 32, 32);

        // 행성 텍스처 정리
        if (solid && gPlanetTextures[i]) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
        // 달
        if (i == 2 && moonSphere.obj) {  // Earth 인덱스 = 2
            glPushMatrix();

            // 지구 기준 공전
            glRotatef(gMoonOrbitAngle, 0.0f, 1.0f, 0.0f);
            glTranslatef(gMoonOrbitRadius, 0.0f, 0.0f);

            // 자전
            glRotatef(gMoonSelfAngle, 0.0f, 1.0f, 0.0f);

            if (solid) {
                gluQuadricDrawStyle(moonSphere.obj, GLU_FILL);
                if (gMoonTexture) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, gMoonTexture);
                }
                else {
                    glDisable(GL_TEXTURE_2D);
                }
            }
            else {
                gluQuadricDrawStyle(moonSphere.obj, GLU_LINE);
                glDisable(GL_TEXTURE_2D);
            }

            gluSphere(moonSphere.obj, moonSphere.size, 24, 24);

            if (solid && gMoonTexture) {
                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_TEXTURE_2D);
            }

            glPopMatrix();
        }

        glPopMatrix();

        // --- 토성 링 그리는 if문 ---
        if (i == 5 && gSaturnRingTexture) {
            // 링 크기: inner/outer 배율로 조정
            float inner = planetSpheres[i].size * 1.2f;
            float outer = planetSpheres[i].size * 1.5f;
            const int segs = 96;

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glDepthMask(GL_FALSE);

            glPushMatrix();
            glRotatef(gPlanets[i].axialTilt, 0.0f, 0.0f, 1.0f);

            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, gSaturnRingTexture);

            // 아주 작은 높이 (planet 중심에서 약간 위)
            const float yOffset = 0.002f;

            glBegin(GL_TRIANGLE_STRIP);
            for (int s = 0; s <= segs; ++s) {
                float a = 2.0f * M_PI * s / segs;
                float cosA = cosf(a), sinA = sinf(a);
                glTexCoord2f((float)s / segs, 0.0f);
                glVertex3f(cosA * outer, yOffset, sinA * outer);
                glTexCoord2f((float)s / segs, 1.0f);
                glVertex3f(cosA * inner, yOffset, sinA * inner);
            }
            glEnd();

            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
            glPopMatrix();

            // 복원
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        glPopMatrix();
    }
     
    // 행성정보 팝업
    int popupIdx = -1;

    // 위성 카메라 모드일 때: 따라가는 행성의 팝업을 항상 표시
    if (gFollowPlanet >= 0) {
        popupIdx = gFollowPlanet;       
    }
    // 글로벌 모드일 때: 마우스 hover 결과 사용
    else if (gHoveredPlanet >= 0) {
        popupIdx = gHoveredPlanet;     
    }

    if (popupIdx >= 0) {
        int idx = popupIdx;
        if (idx >= 0 && idx <= PLANET_COUNT && gPopupTextures[idx]) {
            int width = glutGet(GLUT_WINDOW_WIDTH);
            int height = glutGet(GLUT_WINDOW_HEIGHT);

            int boxW = width / 2;
            int boxH = height / 2;
            int px = width - boxW - 10;
            int py = height - boxH - 10;

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(0, width, 0, height, -1, 1);

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();

            GLboolean lightingWasOn = glIsEnabled(GL_LIGHTING);
            GLboolean depthTestWasOn = glIsEnabled(GL_DEPTH_TEST);
            GLboolean textureWasOn = glIsEnabled(GL_TEXTURE_2D);

            glDisable(GL_LIGHTING);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, gPopupTextures[idx]);

            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0.0f, 1.0f); glVertex2i(px, py);
            glTexCoord2f(1.0f, 1.0f); glVertex2i(px + boxW, py);
            glTexCoord2f(0.0f, 0.0f); glVertex2i(px, py + boxH);
            glTexCoord2f(1.0f, 0.0f); glVertex2i(px + boxW, py + boxH);
            glEnd();

            glBindTexture(GL_TEXTURE_2D, 0);
            if (!textureWasOn) { 
                glDisable(GL_TEXTURE_2D);
            } 
             
            if (depthTestWasOn) {  
                glEnable(GL_DEPTH_TEST);
            }
            else {}
             
            if (lightingWasOn) {
                glEnable(GL_LIGHTING);
            }
            else {}
            glDisable(GL_BLEND);

            glPopMatrix();
            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
        }
    }


    glutSwapBuffers();
}

void drawEclipseFullscreen()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glUseProgram(gEclipseProg);
     
    GLint locRes = glGetUniformLocation(gEclipseProg, "uResolution");
    if (locRes >= 0) {
        glUniform2f(locRes, (float)width, (float)height);
    }
     
    float phase = fmodf(gMoonOrbitAngle, 360.0f) / 360.0f; 
 
    float moonOffset = (0.5f - phase) * 2.0f;  
     
    const float sunR = 0.35f;
    const float moonR = 0.33f;
    const float centerScale = 0.8f;         
    float d = fabsf(moonOffset) * centerScale; 
    float sumR = sunR + moonR;

    float occlusion = 0.0f; 
    if (d < sumR) {
        float t = (sumR - d) / sumR; 
        occlusion = t * t;
        if (occlusion > 1.0f) occlusion = 1.0f;
    }
     
    GLint locOff = glGetUniformLocation(gEclipseProg, "uMoonOffset");
    if (locOff >= 0) glUniform1f(locOff, moonOffset);

    GLint locOcc = glGetUniformLocation(gEclipseProg, "uOcclusion");
    if (locOcc >= 0) glUniform1f(locOcc, occlusion);

    glBindVertexArray(gEclipseVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}




GLvoid Reshape(int w, int h) {
    glViewport(0, 0, w, h);
    width = w;
    height = h;
}

// 키보드 콜백 함수 (디버그 출력 추가)
GLvoid Keyboard(unsigned char key, int x, int y) {
    std::cout << "키 입력 감지: '" << key << "' (ASCII: " << (int)key << ")" << std::endl;

    switch (key) {
    case 't':
    case 'T':
    gPaused = !gPaused;
    std::cout << "장면 " << (gPaused ? "일시정지" : "재개") << std::endl;
    break;
    case 'p':
    case 'P':
        angle = !angle;
        std::cout << "투영 모드 변경: " << (angle ? "직각투영" : "원근투영") << std::endl;
        break;
    case 'm':
    case 'M':
        solid = !solid;
        std::cout << "렌더링 모드 변경: " << (solid ? "솔리드" : "와이어프레임") << std::endl;
        break;
    case 'w':
    case 'W':
        movemat = glm::translate(movemat, glm::vec3(0.0f, 0.1f, 0.0f));
        std::cout << "위로 이동" << std::endl;
        break;
    case 'a':
    case 'A':
        movemat = glm::translate(movemat, glm::vec3(-0.1f, 0.0f, 0.0f));
        std::cout << "왼쪽으로 이동" << std::endl;
        break;
    case 's':
    case 'S':
        movemat = glm::translate(movemat, glm::vec3(0.0f, -0.1f, 0.0f));
        std::cout << "아래로 이동" << std::endl;
        break;
    case 'd':
    case 'D':
        movemat = glm::translate(movemat, glm::vec3(0.1f, 0.0f, 0.0f));
        std::cout << "오른쪽으로 이동" << std::endl;
        break;
 case 'r':
 case 'R':
 movemat = glm::mat4(1.0f);
 big_Matrix = glm::mat4(1.0f);
 CreateMatrix();
 std::cout << "장면 초기화: 태양을 중앙으로 복원" << std::endl;
 break;
    case 'q':
    case 'Q':
        std::cout << "프로그램 종료" << std::endl;
        exit(0);
        break;
    case '+':    // 줌 인 (가까이)
        gCameraZ += 0.1f;         
        if (gCameraZ > -0.5f)
            gCameraZ = -0.5f;      // 너무 안으로 들어가는 것 방지
        std::cout << "줌 인, gCameraZ = " << gCameraZ << std::endl;
        break;

    case '-':    // 줌 아웃 (멀리)
        gCameraZ -= 0.1f;         
        if (gCameraZ < -10.0f)
            gCameraZ = -10.0f;   
        std::cout << "줌 아웃, gCameraZ = " << gCameraZ << std::endl;
        break;
    case ']':   // timeScale 증가
        gTimeScale += 0.05f;
        if (gTimeScale > 5.0f) gTimeScale = 5.0f;   // 최대 5배속
        updateRevolutionSpeed();
        std::cout << "timeScale 증가: " << gTimeScale << std::endl;
        break;

    case '[':   // timeScale 감소
        gTimeScale -= 0.05f;
        if (gTimeScale < 0.001f) gTimeScale = 0.001f; // 0에 가까운 값으로 제한 (역행 방지)
        updateRevolutionSpeed();
        std::cout << "timeScale 감소: " << gTimeScale << std::endl;
        break;

    case 27: // ESC key
        std::cout << "ESC 키로 프로그램 종료" << std::endl;
        exit(0);
        break;
    case '0':  // 일식 모드  
        gEclipseMode = !gEclipseMode;
        std::cout << "일식 모드: " << (gEclipseMode ? "ON" : "OFF") << std::endl;
        break;
    case '1': // 수성
        gFollowPlanet = (gFollowPlanet == 0 ? -1 : 0);
        std::cout << "수성 시점 " << (gFollowPlanet == 0 ? "활성화" : "해제") << std::endl;
        break;
    case '2': // 금성
        gFollowPlanet = (gFollowPlanet == 1 ? -1 : 1);
        std::cout << "금성 시점 " << (gFollowPlanet == 1 ? "활성화" : "해제") << std::endl;
        break;
    case '3': // 지구
        gFollowPlanet = (gFollowPlanet == 2 ? -1 : 2);
        std::cout << "지구 시점 " << (gFollowPlanet == 2 ? "활성화" : "해제") << std::endl;
        break;
    case '4': // 화성
        gFollowPlanet = (gFollowPlanet == 3 ? -1 : 3);
        std::cout << "화성 시점 " << (gFollowPlanet == 3 ? "활성화" : "해제") << std::endl;
        break;
    case '5': // 목성
        gFollowPlanet = (gFollowPlanet == 4 ? -1 : 4);
        std::cout << "목성 시점 " << (gFollowPlanet == 4 ? "활성화" : "해제") << std::endl;
        break;
    case '6': // 토성
        gFollowPlanet = (gFollowPlanet == 5 ? -1 : 5);
        std::cout << "토성 시점 " << (gFollowPlanet == 5 ? "활성화" : "해제") << std::endl;
        break;
    case '7': // 천왕성
        gFollowPlanet = (gFollowPlanet == 6 ? -1 : 6);
        std::cout << "천왕성 시점 " << (gFollowPlanet == 6 ? "활성화" : "해제") << std::endl;
        break;
    case '8': // 해왕성
        gFollowPlanet = (gFollowPlanet == 7 ? -1 : 7);
        std::cout << "해왕성 시점 " << (gFollowPlanet == 7 ? "활성화" : "해제") << std::endl;
        break;
    default:
        std::cout << "알 수 없는 키 입력" << std::endl;
        break;
    }

    glutPostRedisplay();
}

void TimerFunction(int value) {
    if (!gPaused) {
        // 행성 공전 적용
        for (int i = 0; i < PLANET_COUNT; i++) {
            float speed = gRevolutionSpeed[i];
            gPlanetMatrix[i] =
                glm::rotate(glm::mat4(1.0f),
                    glm::radians(speed),
                    glm::vec3(0.0f, 1.0f, 0.0f)) * gPlanetMatrix[i];
        }

        // 자전
        for (int i = 0; i < PLANET_COUNT; i++) {
            float selfSpeed = gSelfRotationSpeed[i];
            gSelfAngle[i] += selfSpeed;
            if (gSelfAngle[i] >= 360.0f || gSelfAngle[i] <= -360.0f)
                gSelfAngle[i] = fmodf(gSelfAngle[i], 360.0f);
        }

        // 태양 자전
        gSunAngle += gSunSelfSpeed;
        if (gSunAngle >= 360.0f || gSunAngle <= -360.0f)
            gSunAngle = fmodf(gSunAngle, 360.0f);

        gMoonOrbitAngle += gMoonOrbitSpeed;
        if (gMoonOrbitAngle >= 360.0f || gMoonOrbitAngle <= -360.0f)
            gMoonOrbitAngle = fmodf(gMoonOrbitAngle, 360.0f);

        gMoonSelfAngle += gMoonSelfSpeed;
        if (gMoonSelfAngle >= 360.0f || gMoonSelfAngle <= -360.0f)
            gMoonSelfAngle = fmodf(gMoonSelfAngle, 360.0f);
 }

 glutPostRedisplay();
 glutTimerFunc(16, TimerFunction,0);
}

void updateRevolutionSpeed() {
    for (int i = 0; i < PLANET_COUNT; i++) {
        // 공전 속도
        float dailyDeg = 360.0f / gPlanets[i].orbitPeriodDays;
        gRevolutionSpeed[i] = dailyDeg * gTimeScale;

        // 자전 속도
        if (gPlanets[i].rotationPeriodDays != 0.0f) {
            float selfDailyDeg = 360.0f / gPlanets[i].rotationPeriodDays;
            gSelfRotationSpeed[i] = selfDailyDeg * gTimeScale * gSelfScale;
        } else {
            gSelfRotationSpeed[i] = 0.0f;
        }
    }

    // 태양 자전 속도 계산
    if (gSunRotationPeriodDays != 0.0f) {
        float sunDailyDeg = 360.0f / gSunRotationPeriodDays;
        gSunSelfSpeed = sunDailyDeg * gTimeScale * gSelfScale;
    } else {
        gSunSelfSpeed = 0.0f;
    }

    const float moonOrbitPeriodDays = 27.32f; 
    float moonDailyDeg = 360.0f / moonOrbitPeriodDays;
    gMoonOrbitSpeed = moonDailyDeg * gTimeScale;          
    gMoonSelfSpeed = moonDailyDeg * gTimeScale * gSelfScale; 
}

//main이 너무 길어져 코드 분리했습니다.
void CreateMatrix() {
 big_Matrix = glm::mat4(1.0f);
 glm::mat4 transmat = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f,0.0f,0.0f));
 glm::mat4 s_transmat = glm::translate(glm::mat4(1.0f), glm::vec3(0.2f,0.0f,0.0f));

 smat[0] = glm::mat4(1.0f);
 smat[1] = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f,0.0f,1.0f));
 smat[2] = glm::rotate(glm::mat4(1.0f), glm::radians(-45.0f), glm::vec3(0.0f,0.0f,1.0f));

 for (int i =0; i <3; i++) {
 Matrix[i] = transmat;
 s_Matrix[i] = s_transmat;
 }
}

void menu() {
 std::cout << "=== Solar System Simulation ===" << std::endl;
 std::cout << "p: 직각투영 / 원근투영" << std::endl;
 std::cout << "m: 솔리드 / 와이어" << std::endl;
 std::cout << "w/a/s/d: 상하좌우 이동" << std::endl;
 std::cout << "+/-: 카메라 줌인/줌아웃" << std::endl;
 std::cout << "[ / ]: 전체 시간 배속 감소/증가" << std::endl;
 std::cout << "0: 일식 모드 토글 (사진 스타일 일식 화면)" << std::endl;
 std::cout << "1~8: 수성~해왕성 시점 토글" << std::endl;
 std::cout << "r: 장면 초기화 (태양을 중앙으로)" << std::endl;
 std::cout << "t: 장면 일시정지/재개" << std::endl;
 std::cout << "q: 종료" << std::endl;
}
