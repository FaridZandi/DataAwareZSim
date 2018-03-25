#include <iostream>
#include <thread>
#include <mutex>
#include <queue>

using namespace std;

int a = 7;

std::mutex my_lock;

void bullshit() {

    for (short i = 0; i < 10 ; i++) {
        my_lock.lock();
        a = a + 2;
        my_lock.unlock();
    }
}

int main() {
    cout << &a << endl;

    std::thread t1(bullshit);
    std::thread t2(bullshit);
    std::thread t3(bullshit);
    std::thread t4(bullshit);
    std::thread t5(bullshit);
    std::thread t6(bullshit);
    std::thread t7(bullshit);
    std::thread t8(bullshit);
    std::thread t9(bullshit);
    std::thread t10(bullshit);
    std::thread t11(bullshit);

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    t6.join();
    t7.join();
    t8.join();
    t9.join();
    t10.join();
    t11.join();

    return 0;
}

