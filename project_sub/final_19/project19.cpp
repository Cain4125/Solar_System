#define _CRT_SECURE_NO_WARNINGS //--- 프로그램 맨 앞에 선언할 것
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <random>
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

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_real_distribution<float> dis(0.0f, 1.0f);

// 객체 클래스 (18.cpp 스타일로 수정)
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

    // 애니메이션 관련 변수
    bool isAnimating = false;
    float animProgress = 0.0f;
    float startPos[3] = { 0.0f };
    float targetPos[3] = { 0.0f };
    float animSpeed = 0.02f;
    int animType = 0; // 0: 없음, 1: 원점 통과, 2: 위/아래 이동

    // 궤도 생성 함수 (18.cpp 스타일로 단순화)
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

    // 18.cpp의 getTransformedPosition을 그대로 사용
    void getTransformedPosition(float outPos[3]) const {
        glm::mat4 baseRotation = glm::mat4(1.0f);
        baseRotation = glm::rotate(baseRotation, glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        baseRotation = glm::rotate(baseRotation, glm::radians(50.0f), glm::vec3(0.0f, -1.0f, 0.0f));

        glm::mat4 modelMatrix = baseRotation;

        modelMatrix = glm::scale(modelMatrix, glm::vec3(
            origin_scale_value[0], origin_scale_value[1], origin_scale_value[2]
        ));

        modelMatrix = glm::rotate(modelMatrix, glm::radians(revolutionAngle),
            glm::vec3(0.0f, 1.0f, 0.0f));

        modelMatrix = glm::translate(modelMatrix, glm::vec3(
            translation[0], translation[1], translation[2]
        ));

        glm::vec4 transformedPos = modelMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        outPos[0] = transformedPos.x;
        outPos[1] = transformedPos.y;
        outPos[2] = transformedPos.z;
    }

    ~Shape() {
        if (obj) {
            gluDeleteQuadric(obj);
        }
    }
};

//--- 전역 변수들 (원본 19.cpp와 유사하게)
Shape orbits[3];
Shape s_orbits[3];
Shape centerSphere;
Shape planetSpheres[3];
Shape moonSpheres[3];
Shape axis; // 축 추가

glm::mat4 Matrix[3];
glm::mat4 s_Matrix[3];
glm::mat4 smat[3];
glm::mat4 big_Matrix;
glm::mat4 scalemat(1.0f);
glm::mat4 zmat(1.0f);
glm::mat4 movemat(1.0f);

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

// 18.cpp의 createAxis 함수 그대로 사용
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

// 18.cpp의 initBuffer 함수 그대로 사용
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

//--- 메인 함수
void main(int argc, char** argv) {
    width = 500;
    height = 500;
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowPosition(100, 100);
    glutInitWindowSize(width, height);
    glutCreateWindow("Example19 - 18 Style");

    glewExperimental = GL_TRUE;
    glewInit();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    make_shaderProgram();

    createAxis(axis); // 축 생성

    menu();
    CreateMatrix();

    // 궤도 생성 및 버퍼 초기화
    for (int i = 0; i < 3; i++) {
        orbits[i].createOrbit(0.5f, 100);
        initBuffer(orbits[i]);
        s_orbits[i].createOrbit(0.2f, 100);
        initBuffer(s_orbits[i]);
    }

    // 구체들 생성
    centerSphere.createSphere(0.2f);
    for (int i = 0; i < 3; i++) {
        planetSpheres[i].createSphere(0.05f);
        moonSpheres[i].createSphere(0.02f);
    }

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
    std::cout << "+/-: 카메라 z축 이동" << std::endl;
    std::cout << "y/Y: 궤도들 반지름 크기조절" << std::endl;
    std::cout << "z/Z: 주변구가 z축으로 회전" << std::endl;
    std::cout << "t: 원점 통과 애니메이션" << std::endl;
    std::cout << "u: 위/아래 이동 애니메이션" << std::endl;
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

void scaling(float s) {
    scalemat = glm::scale(glm::mat4(1.0f), glm::vec3(s, s, s));
}

// 18.cpp의 애니메이션 함수들 그대로 사용
void startOriginPassAnimation() {
    if (isGlobalAnimating) return;

    isGlobalAnimating = true;
    currentAnimationType = 1;

    for (int i = 0; i < 3; i++) {
        planetSpheres[i].isAnimating = true;
        planetSpheres[i].animProgress = 0.0f;
        planetSpheres[i].animType = 1;
        planetSpheres[i].getTransformedPosition(planetSpheres[i].startPos);
    }

    for (int i = 0; i < 3; i++) {
        int targetIndex = (i + 1) % 3;
        planetSpheres[i].targetPos[0] = planetSpheres[targetIndex].startPos[0];
        planetSpheres[i].targetPos[1] = planetSpheres[targetIndex].startPos[1];
        planetSpheres[i].targetPos[2] = planetSpheres[targetIndex].startPos[2];
    }

    std::cout << "원점 통과 애니메이션 시작!" << std::endl;
}

void startUpDownAnimation() {
    if (isGlobalAnimating) return;

    isGlobalAnimating = true;
    currentAnimationType = 2;

    for (int i = 0; i < 3; i++) {
        planetSpheres[i].isAnimating = true;
        planetSpheres[i].animProgress = 0.0f;
        planetSpheres[i].animType = 2;
        planetSpheres[i].getTransformedPosition(planetSpheres[i].startPos);
    }

    for (int i = 0; i < 3; i++) {
        int targetIndex = (i + 1) % 3;
        planetSpheres[i].targetPos[0] = planetSpheres[targetIndex].startPos[0];
        planetSpheres[i].targetPos[1] = planetSpheres[targetIndex].startPos[1];
        planetSpheres[i].targetPos[2] = planetSpheres[targetIndex].startPos[2];
    }

    std::cout << "위/아래 이동 애니메이션 시작!" << std::endl;
}

void updateAnimations() {
    if (!isGlobalAnimating) return;

    bool allAnimationComplete = true;

    for (int i = 0; i < 3; i++) {
        if (planetSpheres[i].isAnimating) {
            planetSpheres[i].animProgress += planetSpheres[i].animSpeed;

            if (planetSpheres[i].animProgress >= 1.0f) {
                planetSpheres[i].animProgress = 1.0f;
                planetSpheres[i].isAnimating = false;

                planetSpheres[i].translation[0] = planetSpheres[i].targetPos[0];
                planetSpheres[i].translation[1] = planetSpheres[i].targetPos[1];
                planetSpheres[i].translation[2] = planetSpheres[i].targetPos[2];
            }
            else {
                allAnimationComplete = false;

                if (currentAnimationType == 1) { // 원점 통과
                    float t = planetSpheres[i].animProgress;
                    glm::vec3 currentPos;

                    if (t <= 0.5f) {
                        float localT = t * 2.0f;
                        currentPos.x = planetSpheres[i].startPos[0] * (1.0f - localT);
                        currentPos.y = planetSpheres[i].startPos[1] * (1.0f - localT);
                        currentPos.z = planetSpheres[i].startPos[2] * (1.0f - localT);
                    }
                    else {
                        float localT = (t - 0.5f) * 2.0f;
                        currentPos.x = planetSpheres[i].targetPos[0] * localT;
                        currentPos.y = planetSpheres[i].targetPos[1] * localT;
                        currentPos.z = planetSpheres[i].targetPos[2] * localT;
                    }

                    planetSpheres[i].translation[0] = currentPos.x;
                    planetSpheres[i].translation[1] = currentPos.y;
                    planetSpheres[i].translation[2] = currentPos.z;
                }
                else if (currentAnimationType == 2) { // 위/아래 이동
                    float t = planetSpheres[i].animProgress;

                    glm::vec3 linearPos;
                    linearPos.x = planetSpheres[i].startPos[0] + (planetSpheres[i].targetPos[0] - planetSpheres[i].startPos[0]) * t;
                    linearPos.y = planetSpheres[i].startPos[1] + (planetSpheres[i].targetPos[1] - planetSpheres[i].startPos[1]) * t;
                    linearPos.z = planetSpheres[i].startPos[2] + (planetSpheres[i].targetPos[2] - planetSpheres[i].startPos[2]) * t;

                    float heightOffset = sin(t * M_PI) * 1.0f;
                    if (i % 2 == 0) {
                        linearPos.y += heightOffset;
                    }
                    else {
                        linearPos.y -= heightOffset;
                    }

                    planetSpheres[i].translation[0] = linearPos.x;
                    planetSpheres[i].translation[1] = linearPos.y;
                    planetSpheres[i].translation[2] = linearPos.z;
                }
            }
        }
    }

    if (allAnimationComplete) {
        isGlobalAnimating = false;
        currentAnimationType = 0;
        std::cout << "애니메이션 완료!" << std::endl;
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

    view = glm::translate(view, glm::vec3(0.0f, 0.0f, -3.0f));
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

    // 궤도 그리기 (셰이더 사용)
    for (int i = 0; i < 3; i++) {
        glm::mat4 orbitMatrix = projection * view * baseRotation * movemat * zmat * scalemat * smat[i];
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(orbitMatrix));
        glBindVertexArray(orbits[i].VAO);
        glDrawElements(GL_LINE_LOOP, orbits[i].index.size(), GL_UNSIGNED_INT, 0);

        glm::mat4 s_orbitMatrix = projection * view * baseRotation * movemat * zmat * scalemat * smat[i] * Matrix[i];
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(s_orbitMatrix));
        glBindVertexArray(s_orbits[i].VAO);
        glDrawElements(GL_LINE_LOOP, s_orbits[i].index.size(), GL_UNSIGNED_INT, 0);
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
    glTranslatef(0.0f, 0.0f, -3.0f);

    // baseRotation 적용
    glMultMatrixf(glm::value_ptr(baseRotation));

    // 스타일 설정
    if (solid) {
        if (centerSphere.obj) gluQuadricDrawStyle(centerSphere.obj, GLU_FILL);
        for (int i = 0; i < 3; i++) {
            if (planetSpheres[i].obj) gluQuadricDrawStyle(planetSpheres[i].obj, GLU_FILL);
            if (moonSpheres[i].obj) gluQuadricDrawStyle(moonSpheres[i].obj, GLU_FILL);
        }
    }
    else {
        if (centerSphere.obj) gluQuadricDrawStyle(centerSphere.obj, GLU_LINE);
        for (int i = 0; i < 3; i++) {
            if (planetSpheres[i].obj) gluQuadricDrawStyle(planetSpheres[i].obj, GLU_LINE);
            if (moonSpheres[i].obj) gluQuadricDrawStyle(moonSpheres[i].obj, GLU_LINE);
        }
    }

    // 중심 구 렌더링 - 빨간색
    glPushMatrix();
    glMultMatrixf(glm::value_ptr(movemat * big_Matrix));
    glColor3f(1.0f, 0.0f, 0.0f);
    if (centerSphere.obj) gluSphere(centerSphere.obj, centerSphere.size, 20, 20);
    glPopMatrix();

    // 행성들 렌더링 - 초록색
    glColor3f(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < 3; i++) {
        glPushMatrix();
        glMultMatrixf(glm::value_ptr(movemat * zmat * scalemat * smat[i] * Matrix[i]));
        if (planetSpheres[i].obj) gluSphere(planetSpheres[i].obj, planetSpheres[i].size, 10, 10);
        glPopMatrix();

        // 달들 렌더링 - 파란색
        glColor3f(0.0f, 0.0f, 1.0f);
        glPushMatrix();
        glMultMatrixf(glm::value_ptr(movemat * zmat * scalemat * smat[i] * Matrix[i] * s_Matrix[i]));
        if (moonSpheres[i].obj) gluSphere(moonSpheres[i].obj, moonSpheres[i].size, 10, 10);
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
    case 'y':
        scale += 0.1f;
        scaling(scale);
        std::cout << "스케일 증가: " << scale << std::endl;
        break;
    case 'Y':
        scale -= 0.1f;
        if (scale < 0.1f) scale = 0.1f;
        scaling(scale);
        std::cout << "스케일 감소: " << scale << std::endl;
        break;
    case 'z':
        z_rotate = true;
        zangle = 2.0f;
        std::cout << "z축 회전 시작 (시계방향)" << std::endl;
        break;
    case 'Z':
        z_rotate = true;
        zangle = -2.0f;
        std::cout << "z축 회전 시작 (반시계방향)" << std::endl;
        break;
    case 't':
    case 'T':
        startOriginPassAnimation();
        break;
    case 'u':
    case 'U':
        startUpDownAnimation();
        break;
    case 'q':
    case 'Q':
        std::cout << "프로그램 종료" << std::endl;
        exit(0);
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
    // 애니메이션 업데이트
    updateAnimations();

    // 기본 회전 (애니메이션 중이 아닐 때만)
    if (!isGlobalAnimating) {
        glm::mat4 a = glm::rotate(glm::mat4(1.0f), glm::radians(1.5f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 b = glm::rotate(glm::mat4(1.0f), glm::radians(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 c = glm::rotate(glm::mat4(1.0f), glm::radians(0.5f), glm::vec3(0.0f, 1.0f, 0.0f));
        Matrix[0] = a * Matrix[0];
        Matrix[1] = b * Matrix[1];
        Matrix[2] = c * Matrix[2];

        glm::mat4 s_a = glm::rotate(glm::mat4(1.0f), glm::radians(2.5f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 s_b = glm::rotate(glm::mat4(1.0f), glm::radians(3.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 s_c = glm::rotate(glm::mat4(1.0f), glm::radians(2.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        s_Matrix[0] = s_a * s_Matrix[0];
        s_Matrix[1] = s_b * s_Matrix[1];
        s_Matrix[2] = s_c * s_Matrix[2];
    }

    if (z_rotate) {
        zmat = glm::rotate(glm::mat4(1.0f), glm::radians(zangle), glm::vec3(0.0f, 0.0f, 1.0f)) * zmat;
    }

    glutPostRedisplay();
    glutTimerFunc(50, TimerFunction, 1);
}