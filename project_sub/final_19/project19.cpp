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
	const char* textureFile;    // 텍스처 파일 경로
};

// 실제 비율을 압축한 값들 (행성 이름, 반지름, 공전 궤도, 공전 주기(속도), 텍스처 파일)
PlanetConfig gPlanets[PLANET_COUNT] = {
    { "Mercury", 0.034f, 0.375f,   87.97f, "texture/mercury.jpg" },
    { "Venus",   0.049f, 0.509f,  224.70f, "texture/venus.jpg"},
    { "Earth",   0.050f, 0.600f,  365.26f, "texture/earth_day.jpg"},
    { "Mars",    0.039f, 0.740f,  686.98f, "texture/mars.jpg"},
    { "Jupiter", 0.130f, 1.368f, 4332.59f,"texture/jupiter.jpg"},
    { "Saturn",  0.121f, 1.849f,10759.22f,"texture/saturn.jpg"},
    { "Uranus",  0.087f, 2.629f,30685.40f,"texture/uranus.jpg"},
    { "Neptune", 0.086f, 3.286f,60189.00f,"texture/neptune.jpg"}
};

// 태양 크기 (화면 기준)
const float SUN_RADIUS = 0.327f;


// 전역 텍스처 핸들 (0: Sun, 1..N: planets)
GLuint gSunTexture = 0;
GLuint gPlanetTextures[PLANET_COUNT] = { 0 };

// Saturn ring 텍스처
GLuint gSaturnRingTexture = 0;

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
Shape axis;                          // 좌표축

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


bool solid = true, angle = false, z_rotate = false;
bool isGlobalAnimating = false;
float scale = 1.0f, zangle = 1.0f;
int currentAnimationType = 0;

GLint width = 500, height = 500; // 18.cpp와 동일하게
GLuint shaderProgramID;
GLuint vertexShader;
GLuint fragmentShader;

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

    make_shaderProgram();

    createAxis(axis); // 축 생성

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
    glutMainLoop();
}

void menu() {
    std::cout << "=== Solar System Simulation (18 Style) ===" << std::endl;
    std::cout << "p: 직각투영 / 원근투영" << std::endl;
    std::cout << "m: 솔리드 / 와이어" << std::endl;
    std::cout << "w/a/s/d: 상하좌우 이동" << std::endl;
    std::cout << "+/-: 카메라 줌인/줌아웃" << std::endl;
	std::cout << "[ / ]: 전체 시간 배속 감소/증가" << std::endl;
    std::cout << "q: 종료" << std::endl;
}

void CreateMatrix() {
    big_Matrix = glm::mat4(1.0f);
    glm::mat4 transmat = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f));
    glm::mat4 s_transmat = glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 0.0f, 0.0f));

    smat[0] = glm::mat4(1.0f);
    smat[1] = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    smat[2] = glm::rotate(glm::mat4(1.0f), glm::radians(-45.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    for (int i = 0; i < 3; i++) {
        Matrix[i] = transmat;
        s_Matrix[i] = s_transmat;
    }
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
    //경계에서 특히 링 텍스처 반복되지 않도록 두 줄 추가
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();
    return tex;
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
}

//--- 출력 콜백 함수 (18.cpp 스타일로 단순화)
GLvoid drawScene() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(shaderProgramID);

    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

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

    // 축 그리기 (18.cpp와 동일)
    glm::mat4 axisMatrix = projection * view * baseRotation;
    glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(axisMatrix));
    glBindVertexArray(axis.VAO);
    glDrawElements(GL_LINES, axis.index.size(), GL_UNSIGNED_INT, 0);

    // 궤도 그리기
    for (int i = 0; i < PLANET_COUNT; i++) {
        glm::mat4 orbitMatrix = projection * view * baseRotation;
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(orbitMatrix));
        glBindVertexArray(orbits[i].VAO);
        glDrawElements(GL_LINE_LOOP, orbits[i].index.size(), GL_UNSIGNED_INT, 0);
    }


    // GLU 객체들 렌더링 (18.cpp와 동일한 방식)
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

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, gCameraZ);

    // baseRotation 적용
    glMultMatrixf(glm::value_ptr(baseRotation));

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
    glPopMatrix();

    // 행성들(태양과 동일한 텍스처 처리 방식, 토성 링 추가 - 구 먼저, 링은 후처리)
    for (int i = 0; i < PLANET_COUNT; i++) {
        float r = gPlanets[i].orbitRadius;
        glPushMatrix();

        // 공전 행렬 적용
        glMultMatrixf(glm::value_ptr(gPlanetMatrix[i]));
        glTranslatef(r, 0.0f, 0.0f);

        // --- 행성(구) 렌더 ---
        if (solid) {
            if (planetSpheres[i].obj) gluQuadricDrawStyle(planetSpheres[i].obj, GLU_FILL);
            if (gPlanetTextures[i]) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, gPlanetTextures[i]);
            } else {
                glDisable(GL_TEXTURE_2D);
            }
        } else {
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

        // --- 토성 링 그리는 if문 ---
        if (i == 5 && gSaturnRingTexture) {
            // 링 크기: inner/outer 배율로 조정
            float inner = planetSpheres[i].size * 1.2f;
            float outer = planetSpheres[i].size * 1.5f;
            const int segs = 96;

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glDepthMask(GL_FALSE);

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

            // 복원
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        glPopMatrix();
    }

    glutSwapBuffers();
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
    default:
        std::cout << "알 수 없는 키 입력" << std::endl;
        break;
    }

    glutPostRedisplay();
}

void TimerFunction(int value) {

    // 행성 공전 적용
    for (int i = 0; i < PLANET_COUNT; i++) {
        float speed = gRevolutionSpeed[i];
        gPlanetMatrix[i] =
            glm::rotate(glm::mat4(1.0f),
                glm::radians(speed),
                glm::vec3(0.0f, 1.0f, 0.0f)) * gPlanetMatrix[i];
    }

    glutPostRedisplay();
    glutTimerFunc(16, TimerFunction, 0);
}

void updateRevolutionSpeed() {
    for (int i = 0; i < PLANET_COUNT; i++) {
        float dailyDeg = 360.0f / gPlanets[i].orbitPeriodDays;
        gRevolutionSpeed[i] = dailyDeg * gTimeScale;
    }
}
