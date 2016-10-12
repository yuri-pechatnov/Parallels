#include <iostream>

using namespace std;

struct Point {
    int x, y;
    void print() {
        cout << x << " " << y << endl;
    }
    Point(int h): y(h) {}
};

int main()
{

    Point A = {.x = 1};
    A.print();
    return 0;
}
