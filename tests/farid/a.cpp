#include <iostream>
#include <thread>
#include <mutex>
#include <queue>

using namespace std;

const int threadNumber = 2;

unsigned char c = 0;
int farid;
std::mutex charLock;
void charFunc() {
    for (short i = 0; i < 1000 ; i++) {
        charLock.lock();
        c = c + 2;
        charLock.unlock();
    }
}

unsigned short int si = 0;
std::mutex shortIntLock;
void shortIntFunc() {
    for (short i = 0; i < 1000 ; i++) {
        shortIntLock.lock();
        si = si + 1;
        si = si + 1;
        shortIntLock.unlock();
    }
}

int integer = 0;
std::mutex intLock;
void intFunc() {
    for (short i = 0; i < 1000 ; i++) {
        intLock.lock();
        integer = integer + 2;
        intLock.unlock();
    }
}

double d = 0;
std::mutex doubleLock;
void doubleFunc() {
    for (short i = 0; i < 1000 ; i++) {
        doubleLock.lock();
        d = d + 2;
        doubleLock.unlock();
    }
}

long double ld = 0;
std::mutex longDoubleLock;
void longDoubleFunc() {
    for (short i = 0; i < 1000 ; i++) {
        longDoubleLock.lock();
        ld = ld + 2;
        longDoubleLock.unlock();
    }
}

int arr[1000];
std::mutex arrayLock[1000];
void arrayFunc() {
    for(int i = 0; i < 1000; i++) {
        arrayLock[i].lock();
        arr[i]++;
        arrayLock[i].unlock();
    }
}

int main() {
    cout << "char address: " << &farid - 1 << endl;
    cout << "short int address: " << &si << endl;
    cout << "int address: " << &integer << endl;
    cout << "double address: " << &d << endl;
    cout << "long double address: " << &ld << endl;
    cout << "int array address: " << arr << endl;

    thread** threads = new thread*[threadNumber * 6];

    for (int j = 0; j < threadNumber; ++j) {
        threads[j] = new thread(charFunc);
    }

    for (int j = threadNumber; j < threadNumber * 2; ++j) {
        threads[j] = new thread(shortIntFunc);
    }

    for (int j = threadNumber * 2; j < threadNumber * 3; ++j) {
        threads[j] = new thread(intFunc);
    }

    for (int j = threadNumber * 3; j < threadNumber * 4; ++j) {
        threads[j] = new thread(doubleFunc);
    }

    for (int j = threadNumber * 4; j < threadNumber * 5; ++j) {
        threads[j] = new thread(longDoubleFunc);
    }

    for (int j = threadNumber * 5; j < threadNumber * 6; ++j) {
        threads[j] = new thread(arrayFunc);
    }

    for (int k = 0; k < threadNumber * 6; ++k) {
        threads[k]->join();
    }

    cout << "char: " << (int)c << endl;
    cout << "short int: " << si << endl;
    cout << "int: " << integer << endl;
    cout << "double: " << d << endl;
    cout << "long double: " << ld << endl;
    cout << "int array: " << arr[100] << endl;

    return 0;
}