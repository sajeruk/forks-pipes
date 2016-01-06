#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>

static double f(double x);
static double calc_integral(double(*f) (double), double a, double b, double h);
static pid_t calc_integral_wrapper(double(*f) (double), double a, double b, double h, size_t threads, double* res);
static int parse_args(int argc, const char** argv, double* a, double* b, double* h, size_t* threads);
static pid_t run_child(double(*f) (double), double a, double b, double h, int* pipe1, int* pipe2);

int main(int argc, const char *argv[])
{
    double a, b, h;
    size_t threads;
    double res;
    pid_t pid;

    if (parse_args(argc, argv, &a, &b, &h, &threads) == -1) {
        printf("%s", "usage: ./integral a b h threads\n");
        return 1;
    }

    pid = calc_integral_wrapper(f, a, b, h, threads, &res);
    if (pid > 0) {
        printf("%lf\n", res);
        while (wait(NULL) > 0) { // ждём окончания дочерних
        }
    }
    return 0;
}

double f(double x) {
    return cos(x);
}

double calc_integral(double(*f) (double), double a, double b, double h) {
    // Суммируем по методу трапеций - формула Котеса
    double sum = (f(a) + f(b)) / 2;
    for (a = a + h; a < b; a += h) {
        sum += f(a);
    }
    return sum * h;
}

int parse_args(int argc, const char** argv, double* a, double* b, double* h, size_t* threads) {
    if (argc != 5) {
        return -1;
    }
    *a = atof(argv[1]);
    *b = atof(argv[2]);
    *h = atof(argv[3]);
    *threads = atoi(argv[4]);
    return 0;
}

pid_t calc_integral_wrapper(double(*f) (double), double a, double b, double h, size_t threads, double* res) {
    pid_t pid = 0;
    double sum = .0;
    double num;
    char sync = '1';
    int pipe1[2]; // child -> father ; returns value
    int pipe2[2]; // sync pipe
    pid_t* pids;
    const double intervalPerThread = (b - a) / threads;
    size_t i;

    if (threads == 1) {
        *res = calc_integral(f, a, b, h);
        return 1;
    }

    pipe(pipe1);
    pipe(pipe2);

    pids = (int*)malloc((threads - 1) * sizeof(pid_t));
    for (i = 0; i < threads - 1; ++i) {
        // если процесс родительский - сохранить информации о порождённом ребёнке
        pid = run_child(f, a + (i + 1) * intervalPerThread, a + (i + 2) * intervalPerThread, h, pipe1, pipe2);
        if (pid == 0) { // child, exit - т.к. за порождение процессов отвечает только 0
            break;
        }
        pids[i] = pid;
    }

    if (pid > 0) { // суммируем, освобождаем память
        close(pipe1[1]);
        close(pipe2[0]);
        sum += calc_integral(f, a, a + intervalPerThread, h); // первый кусок считается в родительском процессе
        for (i = 0; i < threads - 1; ++i) {
            write(pipe2[1], &sync, sizeof(char)); // пишем синхронизирующий байт
            // идея в том, что атормарность write / read на сообщении длины 1 байт кажется очевидной
            // (у нас тут, конечно double, которые сами-то 8-байтовые, но для больших объектов это может иметь больший смысл
            read(pipe1[0], &num, sizeof(double));
            sum += num;
        }
        close(pipe1[0]);
        close(pipe2[1]);
        free(pids);
        *res = sum;
    }
    return pid;
}

static pid_t run_child(double(*f) (double), double a, double b, double h, int* pipe1, int* pipe2) {
    pid_t pid = fork();
    char tmp;
    double res;

    if (pid > 0) {
        return pid;
    }
    close(pipe1[0]); // закрываем ненужные дескрипторы
    close(pipe2[1]);
    res = calc_integral(f, a, b, h);
    read(pipe2[0], &tmp, sizeof(char)); // sync hack -- ждём пока не придёт синхронизирующий байт.
    // а он не придёт, пока сообщение не будет полностью вычитано
    write(pipe1[1], &res, sizeof(double));
    close(pipe1[1]);
    close(pipe2[0]);
    return 0;
}
