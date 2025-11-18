#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <iostream>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
using namespace std;

struct Mesh { GLuint vao = 0, vbo = 0, ebo = 0; GLsizei indexCount = 0; };

struct Body {
    float orbitRadius;
    float orbitSpeed;
    float tiltDeg;
    glm::vec3 color;
    float selfScale;
    float theta = 0.f;
    vector<Body> moons;
};

GLuint prog;
GLint locMVP, locColor;
Mesh sphere;
Body centerBody, p1, p2, p3;
glm::mat4 P(1.f), V(1.f);
float camZ = 6.0f;
bool usePersp = true;
bool wire = false;
glm::vec3 worldOffset(0);
float globalZSpin = 0.f;
float orbitScale = 1.f;

char* filetobuf(const char* file) {
    FILE* f = fopen(file, "rb"); if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(len + 1); fread(buf, 1, len, f); fclose(f); buf[len] = 0; return buf;
}

GLuint makeShader(const char* path, GLenum type) {
    char* src = filetobuf(path); if (!src) { cerr << "read fail " << path << endl; exit(1); }
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, (const GLchar**)&src, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, 1024, nullptr, log); cerr << log << endl; exit(1); }
    free(src); return sh;
}

GLuint makeProgram(const char* vs, const char* fs) {
    GLuint v = makeShader(vs, GL_VERTEX_SHADER), f = makeShader(fs, GL_FRAGMENT_SHADER);
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); cerr << log << endl; exit(1); }
    glDeleteShader(v); glDeleteShader(f); return p;
}

/* 단위 UV-스피어 메쉬 생성 */
Mesh buildSphere(int slices, int stacks) {
    vector<glm::vec3> pos;
    vector<GLushort> idx;
    for (int i = 0; i <= stacks; i++) {
        float v = (float)i / stacks;
        float phi = v * glm::pi<float>();
        float y = cosf(phi);
        float r = sinf(phi);
        for (int j = 0; j <= slices; j++) {
            float u = (float)j / slices;
            float th = u * glm::two_pi<float>();
            pos.emplace_back(r * cosf(th), y, r * sinf(th));
        }
    }
    int stride = slices + 1;
    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            int a = i * stride + j, b = a + stride, c = b + 1, d = a + 1;
            idx.push_back((GLushort)a); idx.push_back((GLushort)b); idx.push_back((GLushort)c);
            idx.push_back((GLushort)a); idx.push_back((GLushort)c); idx.push_back((GLushort)d);
        }
    }
    Mesh m; glGenVertexArrays(1, &m.vao); glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo); glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec3), pos.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glGenBuffers(1, &m.ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLushort), idx.data(), GL_STATIC_DRAW);
    m.indexCount = (GLsizei)idx.size();
    glBindVertexArray(0);
    return m;
}

/* 행성/달 초기화 */
void initBodies() {
    centerBody = { 0.f, 0.f, 0.f, {0.1f,0.4f,1.0f}, 0.8f };
    p1 = { 2.0f, 0.6f,   0.f, {0.9f,0.3f,0.2f}, 0.25f };
    p2 = { 2.0f, 0.6f, 45.f, {0.2f,0.8f,0.4f}, 0.25f };
    p3 = { 2.0f, 0.6f,-45.f, {0.9f,0.8f,0.2f}, 0.25f };
    p1.moons.push_back({ 0.55f, 1.2f, 0.f, {0.85f,0.85f,0.85f}, 0.10f });
    p2.moons.push_back({ 0.55f, 1.2f,0.f, {0.85f,0.85f,0.85f}, 0.10f });
    p3.moons.push_back({ 0.55f, 1.2f,0.f,{0.85f,0.85f,0.85f}, 0.10f });
}

/* MVP 업데이트 */
glm::mat4 makeMVP(const glm::mat4& M) {
    glm::mat4 Vloc = glm::lookAt(glm::vec3(0, 1.3f, camZ), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glm::mat4 Ploc = usePersp
        ? glm::perspective(glm::radians(45.0f), 800.f / 600.f, 0.1f, 100.f)
        : glm::ortho(-4.f, 4.f, -3.f, 3.f, -10.f, 100.f);
    V = Vloc * glm::rotate(glm::mat4(1.f), glm::radians(globalZSpin), glm::vec3(0, 0, 1))
        * glm::translate(glm::mat4(1.f), worldOffset);
    P = Ploc;
    return P * V * M;
}

/* 구 그리기 */
void drawSphere(const glm::mat4& M, const glm::vec3& c) {
    glm::mat4 MVP = makeMVP(M);
    glUseProgram(prog);
    glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
    glUniform4f(locColor, c.r, c.g, c.b, 1.0f);
    glBindVertexArray(sphere.vao);
    glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
    glDrawElements(GL_TRIANGLES, sphere.indexCount, GL_UNSIGNED_SHORT, 0);
}

void drawOrbit(const Body& p) {
    const int segments = 64;
    vector<glm::vec3> pts;
    float R = p.orbitRadius * orbitScale;
    for (int i = 0; i <= segments; ++i) {
        float th = glm::two_pi<float>() * i / segments;
        pts.emplace_back(R * cosf(th), 0, R * sinf(th));
    }

    glm::mat4 tilt = glm::rotate(glm::mat4(1.f), glm::radians(p.tiltDeg), glm::vec3(0, 0, 1));
    glm::mat4 M = tilt;
    glm::mat4 MVP = makeMVP(M);

    glUseProgram(prog);
    glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
    glUniform4f(locColor, 0.7f, 0.7f, 0.7f, 1.0f);

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(glm::vec3), pts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawArrays(GL_LINE_LOOP, 0, (GLsizei)pts.size());

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}
void drawMoonOrbits(const Body& planet) {
    float R = planet.orbitRadius * orbitScale;

    glm::mat4 Mparent =
        glm::rotate(glm::mat4(1.f), glm::radians(planet.tiltDeg), glm::vec3(0, 0, 1)) *
        glm::rotate(glm::mat4(1.f), planet.theta, glm::vec3(0, 1, 0)) *
        glm::translate(glm::mat4(1.f), glm::vec3(R, 0, 0));

    glm::vec3 planetWorldPos = glm::vec3(Mparent[3]);
    glm::mat4 planetTranslate = glm::translate(glm::mat4(1.f), planetWorldPos);
    glm::mat4 planetTilt = glm::rotate(glm::mat4(1.f), glm::radians(planet.tiltDeg), glm::vec3(0, 0, 1));

    for (const auto& m : planet.moons) {
        const int segments = 64;
        vector<glm::vec3> pts;
        float r = m.orbitRadius * orbitScale;
        for (int i = 0; i <= segments; ++i) {
            float th = glm::two_pi<float>() * i / segments;
            pts.emplace_back(r * cosf(th), 0, r * sinf(th));
        }

        glm::mat4 moonTilt = glm::rotate(glm::mat4(1.f), glm::radians(m.tiltDeg), glm::vec3(0, 0, 1));

        glm::mat4 M = planetTranslate * planetTilt * moonTilt;

        glm::mat4 MVP = makeMVP(M);
        glUseProgram(prog);
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
        glUniform4f(locColor, 0.6f, 0.6f, 0.6f, 1.0f);

        GLuint vao, vbo;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(glm::vec3), pts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, FALSE, 0, (void*)0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawArrays(GL_LINE_LOOP, 0, (GLsizei)pts.size());
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
}


/* 한 행성+달 그리기 */
void drawPlanet(Body& p) {
    p.theta += p.orbitSpeed * 0.016f;
    float R = p.orbitRadius * orbitScale;

    glm::mat4 Mparent =
        glm::rotate(glm::mat4(1.f), glm::radians(p.tiltDeg), glm::vec3(0, 0, 1)) *
        glm::rotate(glm::mat4(1.f), p.theta, glm::vec3(0, 1, 0)) *
        glm::translate(glm::mat4(1.f), glm::vec3(R, 0, 0));

    glm::mat4 Mplanet = Mparent * glm::scale(glm::mat4(1.f), glm::vec3(p.selfScale));
    drawSphere(Mplanet, p.color);

    glm::vec3 planetWorldPos = glm::vec3(Mparent[3]);
    glm::mat4 planetTranslate = glm::translate(glm::mat4(1.f), planetWorldPos);
    glm::mat4 planetTilt = glm::rotate(glm::mat4(1.f), glm::radians(p.tiltDeg), glm::vec3(0, 0, 1));

    for (auto& m : p.moons) {
        m.theta += m.orbitSpeed * 0.016f;
        float r = m.orbitRadius * orbitScale;

        glm::mat4 moonTilt = glm::rotate(glm::mat4(1.f), glm::radians(m.tiltDeg), glm::vec3(0, 0, 1));
        glm::mat4 moonRevolve = glm::rotate(glm::mat4(1.f), m.theta, glm::vec3(0, 1, 0));
        glm::mat4 moonTranslate = glm::translate(glm::mat4(1.f), glm::vec3(r, 0, 0));
        glm::mat4 moonScale = glm::scale(glm::mat4(1.f), glm::vec3(m.selfScale));

        glm::mat4 Mmoon =
            planetTranslate *
            planetTilt *
            moonTilt *
            moonRevolve *
            moonTranslate *
            moonScale;

        drawSphere(Mmoon, m.color);
    }
}


/* 디스플레이 */
void display() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glm::mat4 Mcenter = glm::scale(glm::mat4(1.f), glm::vec3(centerBody.selfScale));
    drawSphere(Mcenter, centerBody.color);

    drawOrbit(p1);
    drawOrbit(p2);
    drawOrbit(p3);

    drawMoonOrbits(p1);
    drawMoonOrbits(p2);
    drawMoonOrbits(p3);

    drawPlanet(p1);
    drawPlanet(p2);
    drawPlanet(p3);


    glutSwapBuffers();
}

/* 타이머 */
void timer(int) {
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

/* 키보드 */
void keyboard(unsigned char k, int, int) {
    if (k == 'q' || k == 'Q') exit(0);
    if (k == 'p') usePersp = false;
    if (k == 'P') usePersp = true;
    if (k == 'm') wire = false;
    if (k == 'M') wire = true;
    if (k == 'w') worldOffset.y += 0.1f;
    if (k == 's') worldOffset.y -= 0.1f;
    if (k == 'a') worldOffset.x -= 0.1f;
    if (k == 'd') worldOffset.x += 0.1f;
    if (k == '+') camZ -= 0.2f;
    if (k == '-') camZ += 0.2f;
    if (k == 'y') orbitScale *= 1.05f;
    if (k == 'Y') orbitScale /= 1.05f;
    if (k == 'z') globalZSpin -= 2.f;
    if (k == 'Z') globalZSpin += 2.f;
}

/* 리셰이프 */
void reshape(int w, int h) {
    glViewport(0, 0, w, h);
}

/* 초기화 */
void init() {
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { cerr << "glew fail\n"; exit(1); }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    prog = makeProgram("vertex19.glsl", "fragment19.glsl");
    locMVP = glGetUniformLocation(prog, "uMVP");
    locColor = glGetUniformLocation(prog, "uColor");
    sphere = buildSphere(32, 24);
    initBodies();
}

/* 메인 */
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(800, 600);
    glutCreateWindow("실습19 - 행성 공전");
    init();
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutReshapeFunc(reshape);
    glutTimerFunc(16, timer, 0);
    glutMainLoop();
    return 0;
}
