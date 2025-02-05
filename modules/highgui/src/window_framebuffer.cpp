#include "window_framebuffer.hpp"

#include "opencv2/core/utils/logger.hpp"

#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "opencv2/imgproc.hpp"


namespace cv { namespace highgui_backend {
  
  std::shared_ptr<UIBackend> createUIBackendFramebuffer()
  {
    return std::make_shared<FramebufferBackend>();
  }

  int FramebufferWindow::fb_open_and_get_info()
  {
    int fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd == -1)
    {
      std::cerr << "ERROR_OPENING_FB\n";
      return -1;
    }

    // Get fixed screen information
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix_info)) {
      std::cerr << "ERROR_READING_FIX_INFO\n";
      return -1;
    }

    // Get variable screen information
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var_info)) {
      std::cerr << "EERROR_READING_VAR_INFO\n";
      return -1;
    }

    return fb_fd;
  }
  

  FramebufferWindow::FramebufferWindow()
  {
    std::cout  << "FramebufferWindow()" << std::endl;
    FB_ID = "FramebufferWindow";
    framebuffrer_id = fb_open_and_get_info();
    std::cout  << "FramebufferWindow():: id " << framebuffrer_id << std::endl;
    
    if(framebuffrer_id == -1){
      fb_w = 0;
      fb_h = 0;
      y_offset = 0;
      x_offset = 0;
      bpp = 0;
      line_length = 0;

      return;
    }
    
    fb_w = var_info.xres;
    fb_h = var_info.yres;
    y_offset = var_info.yoffset;
    x_offset = var_info.xoffset;
    bpp = var_info.bits_per_pixel;
    line_length = fix_info.line_length;
    
    std::cout << "= Framebuffer's width, height, bits per pix:\n" 
      << fb_w << " " << fb_h << " " << bpp << "\n\n";
    std::cout << "= Framebuffer's offsets, line length:\n" 
      << y_offset << " " << x_offset << " " << line_length << "\n\n";
    
    // MAP FB TO MEMORY
    screensize = max((__u32)fb_w, var_info.xres_virtual) * 
                 max((__u32)fb_h, var_info.yres_virtual) * bpp / 8;
    fbPointer = (unsigned char*)
      mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, 
        framebuffrer_id, 0);
    if (fbPointer == MAP_FAILED) {
        std::cerr << "ERROR_MAP\n";
        return;
    }

    if(bpp != 32) {
      std::cerr << "Bits per pixel " << bpp << " is not supported" << std::endl;
      return;
    }

    backgroundBuff = Mat(fb_h, fb_w, CV_8UC4);
    int cnt_channel = 4;
    for (int y = y_offset; y < backgroundBuff.rows + y_offset; y++)
    {
        std::memcpy(backgroundBuff.ptr<cv::Vec4b>(y - y_offset), 
                    fbPointer + y * line_length + x_offset, 
                    backgroundBuff.cols * cnt_channel);
    }

    
  }
  
  FramebufferWindow::~FramebufferWindow(){
    
    if(framebuffrer_id == -1) return;
    
    // RESTORE BACKGROUNG
    int cnt_channel = 4;
    for (int y = y_offset; y < backgroundBuff.rows + y_offset; y++)
    {
      std::memcpy(fbPointer + y * line_length + x_offset, 
                  backgroundBuff.ptr<cv::Vec4b>(y - y_offset), 
                  backgroundBuff.cols*cnt_channel);
    }

    if (fbPointer != MAP_FAILED) {
      munmap(fbPointer, screensize);
    }
    close(framebuffrer_id);
  }

  void FramebufferWindow::imshow(InputArray image){
    std::cout  << "FramebufferWindow::imshow(InputArray image)" << std::endl;
    std::cout  << "InputArray image:: size" << image.size() << std::endl;
    if (fbPointer == MAP_FAILED) {
        return;
    }

    if(bpp != 32) {
      std::cerr << "Bits per pixel " << bpp << " is not supported" << std::endl;
      return;
    }
    
    Mat img;
    cvtColor(image, img, COLOR_RGB2RGBA);
    // changing the image size to match the entered width
    double aspect_ratio = static_cast<double>(img.cols) / img.rows;
    int new_width = fb_w;
    int new_height = static_cast<int>(fb_w / aspect_ratio);
    int cnt_channel = img.channels();
    
    //std::cout << "= Initial image width and heigth:\n" << img.cols << " " << img.rows << "\n\n";
    //std::cout << "= Image width / heigth:\n" << aspect_ratio << "\n";
    //std::cout << "= Image count of channels:\n" << img.channels() << "\n";

        // RECIZE IMAGE TO MATCH THE FB SIZE
    if (new_width > fb_w || new_height > fb_h) {
        if (aspect_ratio > static_cast<double>(fb_w) / fb_h) {
            new_width = fb_w;
            new_height = static_cast<int>(fb_w / aspect_ratio);
        } else {
            new_height = fb_h;
            new_width = static_cast<int>(fb_h * aspect_ratio);
        }
    }
    cv::resize(img, img, cv::Size(new_width, new_height), INTER_LINEAR);
    
    std::cout << "= Recized image width and heigth:\n" << img.cols << " " <<  img.rows << "\n\n";
    
    // RESTORE BACKGROUNG
    for (int y = y_offset; y < backgroundBuff.rows + y_offset; y++)
    {
        std::memcpy(fbPointer + y * line_length + x_offset, 
                    backgroundBuff.ptr<cv::Vec4b>(y - y_offset), 
                    backgroundBuff.cols*cnt_channel);
    }


    
    // SHOW IMAGE
    for (int y = y_offset; y < img.rows + y_offset; y++)
    {
        std::memcpy(fbPointer + y * line_length + x_offset, 
                    img.ptr<cv::Vec4b>(y - y_offset), 
                    img.cols*cnt_channel);
    }


  }

  double FramebufferWindow::getProperty(int prop) const{
    std::cout  << "FramebufferWindow::getProperty(int prop:" << prop <<")"<< std::endl; 
    return 0.0;
  }
  bool FramebufferWindow::setProperty(int prop, double value) {
    std::cout  << "FramebufferWindow::setProperty(int prop "<< prop <<", double value "<<value<<")" << std::endl; 
    return false;
  }

  void FramebufferWindow::resize(int width, int height){
    std::cout  << "FramebufferWindow::resize(int width "<< width <<", int height "<< height <<")" << std::endl;
  }
  void FramebufferWindow::move(int x, int y) {
    std::cout  << "FramebufferWindow::move(int x "<< x <<", int y "<< y <<")" << std::endl;
  }

  Rect FramebufferWindow::getImageRect() const {
    std::cout  << "FramebufferWindow::getImageRect()" << std::endl; 
    return Rect(10,10,100,100);
  }

  void FramebufferWindow::setTitle(const std::string& title) {
    std::cout  << "FramebufferWindow::setTitle(... "<< title <<")" << std::endl;
  }

  void FramebufferWindow::setMouseCallback(MouseCallback onMouse, void* userdata ){
    std::cout  << "FramebufferWindow::setMouseCallback(...)" << std::endl;
  }

  std::shared_ptr<UITrackbar> FramebufferWindow::createTrackbar(
      const std::string& name,
      int count,
      TrackbarCallback onChange,
      void* userdata
  ){
    return nullptr;
  }

  std::shared_ptr<UITrackbar> FramebufferWindow::findTrackbar(const std::string& name){
    return nullptr;
  }
  
  const std::string& FramebufferWindow::getID() const  { 
    std::cout  << "getID())" << std::endl; return FB_ID;
  }

  bool FramebufferWindow::isActive() const {
    std::cout  << "isActive()" << std::endl; 
    return true;
  }

  void FramebufferWindow::destroy() {
    std::cout  << "destroy()" << std::endl;
  }

  int FramebufferBackend::OpenInputEvent()
  {
    int fd;
    fd = open("/dev/input/event1", O_RDONLY);
    if (fd == -1) {
        std::cerr << "ERROR_OPENING_INPUT\n";
        return -1;
    }
    return fd;
  }


  FramebufferBackend::FramebufferBackend()
  {
    //eventKey = OpenInputEvent();
    //std::cout  << "FramebufferBackend():: event id " << eventKey << std::endl;
  }
  
  FramebufferBackend::~FramebufferBackend()
  {
    //if(eventKey != -1)
    //{
    //  close(eventKey);
    //}
  }

  void FramebufferBackend::destroyAllWindows() {
    std::cout  << "destroyAllWindows()" << std::endl;
  }

  // namedWindow
  std::shared_ptr<UIWindow> FramebufferBackend::createWindow(
      const std::string& winname,
      int flags
  ){
    std::cout  << "FramebufferBackend::createWindow("<< winname <<", "<<flags<<")" << std::endl;
    return std::make_shared<FramebufferWindow>();
  }

  void FramebufferBackend::initTermios(int echo, int wait) 
  {
    tcgetattr(0, &old); /* grab old terminal i/o settings */
    current = old; /* make new settings same as old settings */
    current.c_lflag &= ~ICANON; /* disable buffered i/o */
    current.c_lflag &= ~ISIG;
    current.c_cc[VMIN]=wait;
    if (echo) {
        current.c_lflag |= ECHO; /* set echo mode */
    } else {
        current.c_lflag &= ~ECHO; /* set no echo mode */
    }
    tcsetattr(0, TCSANOW, &current); /* use these new terminal i/o settings now */
  }

  /* Restore old terminal i/o settings */
  void FramebufferBackend::resetTermios(void) 
  {
    tcsetattr(0, TCSANOW, &old);
  }

  int FramebufferBackend::getch_(int echo, int wait) 
  {
    int ch;
    initTermios(echo, wait);
    ch = getchar();
    rewind(stdin);
    resetTermios();
    return ch;
  }
  bool FramebufferBackend::kbhit()
  {
    int byteswaiting=0;
    initTermios(0, 1);
    if ( ioctl(0, FIONREAD, &byteswaiting) < 0)
    {
      std::cout  << "               ERR byteswaiting " << std::endl;
    }
    resetTermios();
//    std::cout  << "                byteswaiting " << byteswaiting << std::endl;
    
    return byteswaiting > 0;
  }

  int FramebufferBackend::waitKeyEx(int delay) {
    std::cout  << "FramebufferBackend::waitKeyEx(int delay "<< delay <<")" << std::endl; 

    int code = -1;

    if(delay == 0)
    {
      int ch = getch_(0, 1);
      std::cout  << "ch 1 " << (int)ch << std::endl;
      code = ch;
      
      while((ch = getch_(0, 0))>=0)
      {
        std::cout  << "ch 2 " << (int)ch << std::endl;
        code = ch;
      }
    } else {
      if(delay > 0)
      {
        bool f_kbhit = false;
        while(!(f_kbhit = kbhit()) && (delay > 0))
        {
          delay -= 10;
          usleep(10000);
        }          
        if(f_kbhit)
        {
          std::cout  << "f_kbhit " << true << std::endl;
          
          int ch = getch_(0, 1);
          std::cout  << "d ch 1 " << (int)ch << std::endl;
          code = ch;
          
          while((ch = getch_(0, 0))>=0)
          {
            std::cout  << "d ch 2 " << (int)ch << std::endl;
            code = ch;
          }
        }

      }
    }
    
  
    std::cout  << "waitKeyEx:: code "<< code << std::endl; 
    return code; 
  }
  int FramebufferBackend::pollKey()  {
    std::cout  << "FramebufferBackend::pollKey()" << std::endl; 
    return 0;
  }


}
}
