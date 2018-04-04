#include <iostream>
#include <thread>
#include <mutex>
#include <queue>

using namespace std;

int a = 8;

std::mutex my_lock;

void bullshit() {

    for (short i = 0; i < 1000 ; i++) {
        my_lock.lock();
        a = a + 2;
        my_lock.unlock();
    }
}

int main() {
    cout << &a << endl;

    for (int i = 0; i < 100; ++i) {
        a = i;
    }

    cout << a << endl;

    thread** threads = new thread*[16];

    for (int j = 0; j < 16; ++j) {
        threads[j] = new thread(bullshit);
    }

    for (int k = 0; k < 16; ++k) {
        threads[k]->join();
    }
    return 0;
}

