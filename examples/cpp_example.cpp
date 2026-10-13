#include "osi_api.h"
#include <stdio.h> 
 
 /**
  * @brief Please add gcc link flag -lstdc++_nano after -nostdlib in file {CMakeLists.txt.in}
  */

class Shape {
    protected:
        int width, height;
        
    public:
        Shape( int a=0, int b=0)
        {
            width = a;
            height = b;
        }
        virtual ~Shape(){};
        virtual int area();
};
class Rectangle: public Shape{
    public:
        Rectangle( int a=0, int b=0):Shape(a, b) { }
        ~Rectangle(){RTI_LOG("Destory: Rectangle");}
        int area (){ 
            RTI_LOG("Rectangle class area : %d", (width * height));
            return (width * height); 
        }
};
class Triangle: public Shape{
    public:
        Triangle( int a=0, int b=0):Shape(a, b) { }
        ~Triangle(){RTI_LOG("Destory: Triangle");}
        int area (){ 
            RTI_LOG("Triangle class area : %d", (width * height / 2));
            return (width * height / 2); 
        }
};

extern "C"{
int appimg_enter(void *param)
{
    Shape *shape;
    Rectangle *rec = new Rectangle(10,7);
    Triangle *tri = new Triangle(10,5);

    // 存储矩形的地址
    shape = rec;
    // 调用矩形的求面积函数 area
    shape->area();
    delete shape;

    // 存储三角形的地址
    shape = tri;
    // 调用三角形的求面积函数 area
    shape->area();

    delete shape;
    return 0;
}

void appimg_exit(void)
{
    RTI_LOG("application image exit");
}
}