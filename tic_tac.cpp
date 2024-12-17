#include <windows.h>
#include <fstream>
#include <string>
#include <ctime>
#include <iostream>
#include <chrono>
#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "User32.lib")
using namespace std;

// Глобальные переменные
UINT WM_USER_UPDATE_CELLS;
int N = 10; // Размер поля по умолчанию
int cellWidth;
int cellHeight;
int windowHeight = 240; // Высота окна по умолчанию
int windowWidth = 320; //Ширина окна по умолчанию
COLORREF backgroundColor = RGB(0, 0, 255); // Синий фон
COLORREF gridColor = RGB(255, 0, 0); // Красная сетка
COLORREF circleColor = RGB(255, 255, 255); // Белый цвет для кругов
COLORREF crossColor = RGB(255, 255, 0); // Желтый цвет для крестиков

COLORREF startColor = RGB(0, 0, 255); // Start color for gradient
COLORREF endColor = RGB(0, 255, 0);   // End color for gradient
COLORREF currentColor = startColor;
double gradientStep = 0.05;  // Пример значения, вы можете изменить в соответствии с вашими предпочтениями
int gradientThreadPriority = THREAD_PRIORITY_NORMAL;  // Приоритет потока отрисовки фона градиента
bool isThreadPaused = false;  // Флаг для отслеживания состояния паузы потока

struct Cell {
    bool isFilled;
    bool isCircle;
    Cell() : isFilled(false), isCircle(false) {}
};

// Добавляем двумерный массив для отслеживания состояния клеток
Cell** cells;

struct SharedData {
    Cell cells[10][10];
};

SharedData* sharedData;

HANDLE hMapFile;
HANDLE hThread;
void InitializeCells(HWND hwnd) {
    // Освобождаем предыдущую разделяемую память, если она существует
    if (hMapFile != NULL) {
        UnmapViewOfFile(sharedData);
        CloseHandle(hMapFile);
    }

    // Создаем новую разделяемую память
    hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedData), L"MySharedMemory");
    if (hMapFile == NULL) {
        // Обработка ошибок
        return;
    }

    // Получаем указатель на разделяемую память
    sharedData = (SharedData*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (sharedData == NULL) {
        // Обработка ошибок
        CloseHandle(hMapFile);
        return;
    }

    // Обновляем размер поля N
    cellWidth = windowWidth / N;
    cellHeight = windowHeight / N;

    // Очищаем окно
    InvalidateRect(hwnd, NULL, TRUE);
}
void SaveConfiguration() {
    ofstream configFile("config.dat", ios::binary);
    if (configFile.is_open()) {
        configFile.write(reinterpret_cast<char*>(&N), sizeof(N));
        configFile.write(reinterpret_cast<char*>(&windowWidth), sizeof(windowWidth));
        configFile.write(reinterpret_cast<char*>(&windowHeight), sizeof(windowHeight));
        configFile.write(reinterpret_cast<char*>(&backgroundColor), sizeof(backgroundColor));
        configFile.write(reinterpret_cast<char*>(&gridColor), sizeof(gridColor));
        configFile.close();
    }
}


void LoadConfiguration() {
    ifstream configFile("config.dat", ios::binary);
    if (configFile.is_open()) {
        configFile.read(reinterpret_cast<char*>(&N), sizeof(N));
        configFile.read(reinterpret_cast<char*>(&windowWidth), sizeof(windowWidth));
        configFile.read(reinterpret_cast<char*>(&windowHeight), sizeof(windowHeight));
        configFile.read(reinterpret_cast<char*>(&backgroundColor), sizeof(backgroundColor));
        configFile.read(reinterpret_cast<char*>(&gridColor), sizeof(gridColor));
        configFile.close();
    }
}

DWORD WINAPI UpdateGradientColors(LPVOID lpParam) {
    HWND hwnd = static_cast<HWND>(lpParam);

    while (true) {
        if (!isThreadPaused) {
            // Linear interpolation between startColor and endColor
            int r = static_cast<int>((1.0 - gradientStep) * GetRValue(currentColor) + gradientStep * GetRValue(endColor));
            int g = static_cast<int>((1.0 - gradientStep) * GetGValue(currentColor) + gradientStep * GetGValue(endColor));
            int b = static_cast<int>((1.0 - gradientStep) * GetBValue(currentColor) + gradientStep * GetBValue(endColor));

            currentColor = RGB(r, g, b);

            // Signal the main thread to redraw the background
            SendMessage(hwnd, WM_USER_UPDATE_CELLS, 0, 0);
        }

        // Sleep for a short duration to control the update rate
        Sleep(150);  // Smaller sleep duration for a faster update
    }

    return 0;
}




// Function to draw the gradient background
void DrawGradientBackground(HWND hwnd) {
    HDC hdc = GetDC(hwnd);

    // Create a gradient brush
    TRIVERTEX vertices[2];
    GRADIENT_RECT gradientRect;

    vertices[0].x = 0;
    vertices[0].y = 0;
    vertices[0].Red = GetRValue(currentColor) << 8;
    vertices[0].Green = GetGValue(currentColor) << 8;
    vertices[0].Blue = GetBValue(currentColor) << 8;
    vertices[0].Alpha = 0x0000;

    vertices[1].x = windowWidth;
    vertices[1].y = windowHeight;
    vertices[1].Red = GetRValue(endColor) << 8;
    vertices[1].Green = GetGValue(endColor) << 8;
    vertices[1].Blue = GetBValue(endColor) << 8;
    vertices[1].Alpha = 0x0000;

    gradientRect.UpperLeft = 0;
    gradientRect.LowerRight = 1;

    // Draw the gradient background
    GradientFill(hdc, vertices, 2, &gradientRect, 1, GRADIENT_FILL_RECT_H);

    ReleaseDC(hwnd, hdc);
}

void DrawShapes(HWND hwnd) {
    HDC hdc = GetDC(hwnd);

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (sharedData->cells[i][j].isFilled) {
                if (sharedData->cells[i][j].isCircle) {
                    HBRUSH circleBrush = CreateSolidBrush(circleColor);
                    SelectObject(hdc, circleBrush);
                    Ellipse(hdc, i * cellWidth, j * cellHeight, (i + 1) * cellWidth, (j + 1) * cellHeight);
                    DeleteObject(circleBrush);
                }
                else {
                    int savedState = SaveDC(hdc);

                    HPEN crossPen = CreatePen(PS_SOLID, 2, crossColor);
                    SelectObject(hdc, crossPen);
                    MoveToEx(hdc, i * cellWidth, j * cellHeight, NULL);
                    LineTo(hdc, (i + 1) * cellWidth, (j + 1) * cellHeight);
                    MoveToEx(hdc, i * cellWidth, (j + 1) * cellHeight, NULL);
                    LineTo(hdc, (i + 1) * cellWidth, j * cellHeight);
                    DeleteObject(crossPen);

                    RestoreDC(hdc, savedState);
                }
            }
        }
    }

    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_USER_UPDATE_CELLS) {
        // Update gradient colors
        currentColor = RGB(rand() % 256, rand() % 256, rand() % 256);

        // Redraw the background with the new gradient
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        return 0;
    }
    switch (msg) {
    case WM_CREATE: {
        LoadConfiguration();
        InitializeCells(hwnd);
        SetWindowPos(hwnd, NULL, 0, 0, windowWidth, windowHeight, SWP_NOMOVE | SWP_NOZORDER);
        break;
    }
    case WM_DESTROY: {
        UnmapViewOfFile(sharedData);
        CloseHandle(hMapFile);
        SaveConfiguration();
        PostQuitMessage(0); // Постоянное завершение приложения
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Draw the gradient background
        DrawGradientBackground(hwnd);

        // Рисуем сетку
        HPEN pen = CreatePen(PS_SOLID, 1, gridColor);
        SelectObject(hdc, pen);

        for (int i = 0; i < N; ++i) {
            // Горизонтальные линии
            MoveToEx(hdc, 0, i * cellHeight, NULL);
            LineTo(hdc, windowWidth, i * cellHeight);

            // Вертикальные линии
            MoveToEx(hdc, i * cellWidth, 0, NULL);
            LineTo(hdc, i * cellWidth, windowHeight);
        }

        DeleteObject(pen);   // Освобождение пера

        // Функция для рисования кругов и крестиков
        DrawShapes(hwnd);

        EndPaint(hwnd, &ps);
        break;
    }


    case WM_SIZE: {

        windowWidth = LOWORD(lParam);
        windowHeight = HIWORD(lParam);

        cellWidth = windowWidth / N;
        cellHeight = windowHeight / N;


        InvalidateRect(hwnd, NULL, TRUE);
        break;
    }



    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        int cellX = x / cellWidth;
        int cellY = y / cellHeight;

        if (cellX >= 0 && cellX < N && cellY >= 0 && cellY < N && !sharedData->cells[cellX][cellY].isFilled) {
            sharedData->cells[cellX][cellY].isFilled = true;
            sharedData->cells[cellX][cellY].isCircle = true;
            InvalidateRect(hwnd, NULL, TRUE);
            SendMessage(HWND_BROADCAST, WM_USER_UPDATE_CELLS, 0, 0);
        }
        break;
    }

    case WM_RBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        int cellX = x / cellWidth;
        int cellY = y / cellHeight;

        if (cellX >= 0 && cellX < N && cellY >= 0 && cellY < N && !sharedData->cells[cellX][cellY].isFilled) {
            sharedData->cells[cellX][cellY].isFilled = true;
            sharedData->cells[cellX][cellY].isCircle = false;
            InvalidateRect(hwnd, NULL, TRUE);

            SendMessage(HWND_BROADCAST, WM_USER_UPDATE_CELLS, 0, 0);
        }
        break;
    }


    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_ESCAPE: {
        case 'Q': // Добавляем обработку клавиши 'Q'
            if (GetKeyState(VK_CONTROL) & 0x8000) { // Проверяем состояние Ctrl
                PostQuitMessage(0);
            }
            break;
        }
        case VK_RETURN: {
            backgroundColor = RGB(rand() % 256, rand() % 256, rand() % 256);
            InvalidateRect(hwnd, NULL, TRUE);
            SaveConfiguration();
            break;
        }
        case 'C': {
            if (GetKeyState(VK_SHIFT) & 0x8000) { // Shift+C
                system("notepad.exe");
            }
            break;
        }
        case '1':
            gradientThreadPriority = THREAD_PRIORITY_NORMAL;
            break;
        case '2':
            gradientThreadPriority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case '3':
            gradientThreadPriority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case '4':
            gradientThreadPriority = THREAD_PRIORITY_HIGHEST;
            break;
        case VK_SPACE:
            isThreadPaused = !isThreadPaused;
            break;
        }

        // Set the thread priority
        SetThreadPriority(hThread, gradientThreadPriority);

        break;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0) {
            // При прокрутке вверх
            int r = GetRValue(gridColor);
            int g = GetGValue(gridColor);
            int b = GetBValue(gridColor);
            r = min(r + 10, 255);
            g = min(g + 10, 255);
            b = min(b + 10, 255);
            gridColor = RGB(r, g, b);
            InvalidateRect(hwnd, NULL, TRUE);
            SaveConfiguration();
        }
        else if (delta < 0) {
            // При прокрутке вниз
            int r = GetRValue(gridColor);
            int g = GetGValue(gridColor);
            int b = GetBValue(gridColor);
            r = max(r - 10, 0);
            g = max(g - 10, 0);
            b = max(b - 10, 0);
            gridColor = RGB(r, g, b);
            InvalidateRect(hwnd, NULL, TRUE);
            SaveConfiguration();
        }
        break;
    }
    case WM_CLOSE: {
        SaveConfiguration();
        PostQuitMessage(0);
        break;
    }
    default: {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    srand(time(NULL));
    WM_USER_UPDATE_CELLS = RegisterWindowMessage(L"WM_USER_UPDATE_CELLS");
    // Регистрация класса окна
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, (HBRUSH)(COLOR_WINDOW + 1), NULL, L"Кружок и крестик", NULL };
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Не удалось зарегистрировать класс окна.", L"Ошибка", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Создание окна
    HWND hwnd = CreateWindow(wc.lpszClassName, L"Кружок и крестик", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (hwnd == NULL) {
        MessageBox(NULL, L"Не удалось создать окно.", L"Ошибка", MB_ICONERROR | MB_OK);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Анализ аргументов командной строки
    if (lpCmdLine && lpCmdLine[0] != '\0') {
        int cmdN = atoi(lpCmdLine);
        if (cmdN > 0) {
            N = cmdN;
            SaveConfiguration(); // Сохраняем новую конфигурацию
        }
    }
    HANDLE hThread = CreateThread(NULL, 0, UpdateGradientColors, hwnd, 0, NULL);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CloseHandle(hThread);
    UnmapViewOfFile(sharedData);
    CloseHandle(hMapFile);

    return msg.wParam;
}
