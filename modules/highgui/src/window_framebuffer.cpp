#include "window_framebuffer.hpp"

#include <opencv2/core/utils/configuration.private.hpp>
#include <opencv2/core/utils/logger.defines.hpp>
#ifdef NDEBUG
#define CV_LOG_STRIP_LEVEL CV_LOG_LEVEL_DEBUG + 1
#else
#define CV_LOG_STRIP_LEVEL CV_LOG_LEVEL_VERBOSE + 1
#endif
#include <opencv2/core/utils/logger.hpp>

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

#include "XWDFile.h"

namespace cv { namespace highgui_backend {
  
  std::shared_ptr<UIBackend> createUIBackendFramebuffer()
  {
    return std::make_shared<FramebufferBackend>();
  }

  static std::string& getFBMode()
  {
    static std::string fbModeOpenCV = 
      cv::utils::getConfigurationParameterString("OPENCV_HIGHGUI_FB_MODE", "");
    static std::string fbModeDef = "FB";
    
    if(!fbModeOpenCV.empty()) return fbModeOpenCV;
    return fbModeDef;
  }

  static std::string& getFBFileName()
  {
    static std::string fbFileNameFB = 
      cv::utils::getConfigurationParameterString("FRAMEBUFFER", "");
    static std::string fbFileNameOpenCV = 
      cv::utils::getConfigurationParameterString("OPENCV_HIGHGUI_FB_DEVICE", "");
    static std::string fbFileNameDef = "/dev/fb0";
    
    if(!fbFileNameOpenCV.empty()) return fbFileNameOpenCV;
    if(!fbFileNameFB.empty()) return fbFileNameFB;
    return fbFileNameDef;
  }


  FramebufferWindow::FramebufferWindow(FramebufferBackend &_backend, int _flags): 
    backend(_backend), flags(_flags)
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::FramebufferWindow()");
    FB_ID = "FramebufferWindow";
    windowRect = Rect(0,0, backend.getFBWidth(), backend.getFBHeight());
  }
  
  FramebufferWindow::~FramebufferWindow()
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::~FramebufferWindow()");
  }

  void FramebufferWindow::imshow(InputArray image)
  {
    currentImg = image.getMat().clone();
    
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::imshow(InputArray image)");
    CV_LOG_INFO(NULL, "UI: InputArray image: "
      << cv::typeToString(image.type()) << " size " << image.size());
      
    if((currentImg.size().width <= 0) && (currentImg.size().height <= 0))
    {  
      return;
    }

    Mat img(image.getMat().clone());
    switch(img.channels()){
      case 1:
        switch(img.type())
        {
        case CV_8S:
            cv::convertScaleAbs(img, img, 1, 127);
            break;
        case CV_16S:
            cv::convertScaleAbs(img, img, 1/255., 127);
            break;
        case CV_16U:
            cv::convertScaleAbs(img, img, 1/255.);
            break;
        case CV_32F:
        case CV_64F: // assuming image has values in range [0, 1)
            img.convertTo(img, CV_8U, 255., 0.);
            break;
        }
        cvtColor(img, img, cv::COLOR_GRAY2RGB); 
      break;
      case 3:
        convertToShow(img, img);
      break;
      case 4:
        convertToShow(img, img, true);
      break;
    }
    cvtColor(img, img, COLOR_RGB2BGRA);
    
    int newWidth = windowRect.width;
    int newHeight = windowRect.height;
    int cntChannel = img.channels();
    cv::Size imgSize = currentImg.size();
    
    
    if(flags & WINDOW_AUTOSIZE)
    {
      windowRect.width  = imgSize.width;
      windowRect.height = imgSize.height;
      newWidth = windowRect.width;
      newHeight = windowRect.height;
    }
    
    if(flags & WINDOW_FREERATIO)
    {
      newWidth = windowRect.width;
      newHeight = windowRect.height;
    }
    
    if(flags & WINDOW_KEEPRATIO)
    {
      double aspect_ratio = ((double)img.cols) / img.rows;
      newWidth  = windowRect.width;
      newHeight = (int)(windowRect.width / aspect_ratio);

      if (newHeight > windowRect.height) {
        newWidth = (int)(windowRect.height * aspect_ratio);
        newHeight = windowRect.height;
      }
    }

    if((newWidth != img.cols) && (newHeight != img.rows))
    {
      cv::resize(img, img, cv::Size(newWidth, newHeight), INTER_LINEAR);
    }    
        
    CV_LOG_INFO(NULL, "UI: Formated image: "
      << cv::typeToString(img.type()) << " size " << img.size());

    if(backend.getMode() == FB_MODE_EMU)
    {
      CV_LOG_WARNING(NULL, "UI: FramebufferWindow::imshow is used in EMU mode");
      return;
    }

    if (backend.getFBPointer() == MAP_FAILED) {
      CV_LOG_ERROR(NULL, "UI: Framebuffer is not mapped");
      return;
    }
        
    // SHOW IMAGE
    int xOffset = backend.getFBXOffset();
    int yOffset = backend.getFBYOffset();
    int lineLength = backend.getFBLineLength();
    
    int showRows = min((windowRect.y + img.rows), backend.getFBHeight() - yOffset);
    int showCols = min((windowRect.x + img.cols), backend.getFBWidth() - xOffset);
    
    int dx_w = windowRect.x;
    int dy_w = windowRect.y;

    int start_y_w = 0;
    int start_x_w = 0;
    
    if(dy_w < 0) start_y_w = -dy_w;
    if(dx_w < 0) start_x_w = -dx_w;
    
    if(dy_w < 0) dy_w = 0;
    if(dx_w < 0) dx_w = 0;
    
    showRows -= dy_w;
    showCols -= dx_w;
    
    
    for (int y = yOffset; y < showRows + yOffset; y++)
    {
        std::memcpy(backend.getFBPointer() + (y + windowRect.y) * lineLength + 
                    xOffset + windowRect.x, 
                    img.ptr<unsigned char>(y - yOffset + start_y_w) + start_x_w * cntChannel, 
                    showCols * cntChannel);
    }
  }

  double FramebufferWindow::getProperty(int prop) const
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::getProperty(int prop: " << prop << ")");
    CV_LOG_WARNING(NULL, "UI: getProperty (not supported)");

    return 0.0;
  }

  bool FramebufferWindow::setProperty(int prop, double value) 
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::setProperty(int prop " 
      << prop << ", value " << value << ")");
    CV_LOG_WARNING(NULL, "UI: setProperty (not supported)");

    return false;
  }

  void FramebufferWindow::resize(int width, int height)
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::resize(int width " 
      << width <<", height " << height << ")");
    
    CV_Assert(width > 0);
    CV_Assert(height > 0);
    
    if(flags & WINDOW_AUTOSIZE)
    {
      windowRect.width = width;
      windowRect.height = height;
      
      if((currentImg.size().width > 0) && (currentImg.size().height > 0))
      {  
        imshow(currentImg);
      }
    }
  }
  void FramebufferWindow::move(int x, int y) 
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::move(int x " << x << ", y " << y <<")");

    windowRect.x = x;
    windowRect.y = y;

    if((currentImg.size().width > 0) && (currentImg.size().height > 0))
    {  
      imshow(currentImg);
    }
  }

  Rect FramebufferWindow::getImageRect() const 
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::getImageRect()");
    return windowRect;
  }

  void FramebufferWindow::setTitle(const std::string& title) 
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::setTitle(" << title << ")");
    CV_LOG_WARNING(NULL, "UI: setTitle (not supported)");
  }

  void FramebufferWindow::setMouseCallback(MouseCallback /*onMouse*/, void* /*userdata*/ )
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::setMouseCallback(...)");
    CV_LOG_WARNING(NULL, "UI: setMouseCallback (not supported)");
  }

  std::shared_ptr<UITrackbar> FramebufferWindow::createTrackbar(
      const std::string& /*name*/,
      int /*count*/,
      TrackbarCallback /*onChange*/,
      void* /*userdata*/)
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::createTrackbar(...)");
    CV_LOG_WARNING(NULL, "UI: createTrackbar (not supported)");
    return nullptr;
  }

  std::shared_ptr<UITrackbar> FramebufferWindow::findTrackbar(const std::string& /*name*/)
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::findTrackbar(...)");
    CV_LOG_WARNING(NULL, "UI: findTrackbar (not supported)");
    return nullptr;
  }
  
  const std::string& FramebufferWindow::getID() const  
  { 
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::getID()");
    return FB_ID;
  }

  bool FramebufferWindow::isActive() const 
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::isActive()");
    return true;
  }

  void FramebufferWindow::destroy() 
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::destroy()");
  }

//FramebufferBackend

  int FramebufferBackend::fbOpenAndGetInfo()
  {
    std::string fbFileName = getFBFileName();
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::The following is used as a framebuffer file: \n" << fbFileName);
    
    int fb_fd = open(fbFileName.c_str(), O_RDWR);
    if (fb_fd == -1)
    {
      CV_LOG_ERROR(NULL, "UI: can't open framebuffer");
      return -1;
    }

    // Get fixed screen information
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fixInfo)) {
      CV_LOG_ERROR(NULL, "UI: can't read fix info for framebuffer");
     return -1;
    }

    // Get variable screen information
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &varInfo)) {
      CV_LOG_ERROR(NULL, "UI: can't read var info for framebuffer");
      return -1;
    }
    
    CV_LOG_INFO(NULL, "UI: framebuffer info: \n" 
      << "   red offset " <<    varInfo.red.offset << " length " <<   varInfo.red.length << "\n"
      << " green offset " <<  varInfo.green.offset << " length " << varInfo.green.length << "\n"
      << "  blue offset " <<   varInfo.blue.offset << " length " <<  varInfo.blue.length << "\n"
      << "transp offset " << varInfo.transp.offset << " length " <<varInfo.transp.length << "\n"
      << "bits_per_pixel " << varInfo.bits_per_pixel);
      
    if((  varInfo.red.offset != 16) && (  varInfo.red.length != 8) &&
       (varInfo.green.offset != 8 ) && (varInfo.green.length != 8) &&
       ( varInfo.blue.offset != 0 ) && ( varInfo.blue.length != 8) && 
       (varInfo.bits_per_pixel != 32) )
    {
      close(fb_fd);
      CV_LOG_ERROR(NULL, "UI: Framebuffer format is not supported (use BGRA format with bits_per_pixel = 32)");
      return -1;
    }

    fbWidth        = varInfo.xres;
    fbHeight       = varInfo.yres;
    fbXOffset      = varInfo.yoffset;
    fbYOffset      = varInfo.xoffset;
    fbBitsPerPixel = varInfo.bits_per_pixel;
    fbLineLength   = fixInfo.line_length;

    // MAP FB TO MEMORY
    fbScreenSize = max((__u32)fbWidth , varInfo.xres_virtual) * 
                   max((__u32)fbHeight, varInfo.yres_virtual) * 
                   fbBitsPerPixel / 8;
                 
    fbPointer = (unsigned char*)
      mmap(0, fbScreenSize, PROT_READ | PROT_WRITE, MAP_SHARED, 
        fb_fd, 0);
        
    if (fbPointer == MAP_FAILED) {
      CV_LOG_ERROR(NULL, "UI: can't mmap framebuffer");
      return -1;
    }

    return fb_fd;
  }
  
  int FramebufferBackend::XvfbOpenAndGetInfo()
  {
    std::string fbFileName = getFBFileName();
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::The following is used as a framebuffer file: \n" << fbFileName);
    
    int fb_fd = open(fbFileName.c_str(), O_RDWR);
    if (fb_fd == -1)
    {
      CV_LOG_ERROR(NULL, "UI: can't open framebuffer");
      return -1;
    }

    XWDFileHeader *xwd_header;
    
    xwd_header = (XWDFileHeader*) mmap(NULL, sizeof(XWDFileHeader), PROT_READ, MAP_SHARED, fb_fd, 0);

    if (xwd_header == MAP_FAILED) {
      CV_LOG_ERROR(NULL, "UI: can't mmap xwd header");
      return -1;
    }
    
    if( C32INT(&(xwd_header->pixmap_format)) != ZPixmap ){
      CV_LOG_ERROR(NULL, "Unsupported pixmap format: " << xwd_header->pixmap_format);
      return -1;
    }

    if( xwd_header->xoffset != 0 ){
      CV_LOG_ERROR(NULL, "UI: Unsupported xoffset value: " << xwd_header->xoffset );
      return -1;
    }
    
    unsigned int r = C32INT(&(xwd_header->  red_mask));
    unsigned int g = C32INT(&(xwd_header->green_mask));
    unsigned int b = C32INT(&(xwd_header-> blue_mask));

    fbWidth        = C32INT(&(xwd_header->pixmap_width));
    fbHeight       = C32INT(&(xwd_header->pixmap_height));
    fbXOffset      = 0;
    fbYOffset      = 0;
    fbLineLength   = C32INT(&(xwd_header->bytes_per_line));
    fbBitsPerPixel = C32INT(&(xwd_header->bits_per_pixel));
    
    CV_LOG_INFO(NULL, "UI: XVFB info: \n" 
      << "   red_mask " << r << "\n"
      << " green_mask " << g << "\n"
      << "  blue_mask " << b << "\n"
      << "bits_per_pixel " << fbBitsPerPixel);
    
    if((r != 16711680 ) && (g != 65280 ) && (b != 255 ) && 
       (fbBitsPerPixel != 32) ){
      CV_LOG_ERROR(NULL, "UI: Framebuffer format is not supported (use BGRA format with bits_per_pixel = 32)");
      return -1;
    }

    xvfb_len_header = C32INT(&(xwd_header->header_size));
    xvfb_len_colors = sizeof(XWDColor) * C32INT(&(xwd_header->ncolors));
    xvfb_len_pixmap = C32INT(&(xwd_header->bytes_per_line)) * C32INT(&(xwd_header->pixmap_height));
    munmap(xwd_header, sizeof(XWDFileHeader));

    fbScreenSize = xvfb_len_header + xvfb_len_colors + xvfb_len_pixmap;
    xwd_header = (XWDFileHeader*) mmap(NULL, fbScreenSize,  PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    fbPointer = (unsigned char*)xwd_header ;
    fbPointer_dist = xvfb_len_header + xvfb_len_colors;

    return fb_fd;
  }

  fb_var_screeninfo &FramebufferBackend::getVarInfo()
  {
      return varInfo;
  }

  fb_fix_screeninfo &FramebufferBackend::getFixInfo()
  {
    return fixInfo;
  }

  int FramebufferBackend::getFramebuffrerID()
  {
    return fbID;
  }

  int FramebufferBackend::getFBWidth()
  {
    return fbWidth;
  }

  int FramebufferBackend::getFBHeight()
  {
    return fbHeight;
  }

  int FramebufferBackend::getFBXOffset()
  {
    return fbXOffset;
  }

  int FramebufferBackend::getFBYOffset()
  {
    return fbYOffset;
  }

  int FramebufferBackend::getFBBitsPerPixel()
  {
    return fbBitsPerPixel;
  }

  int FramebufferBackend::getFBLineLength()
  {
    return fbLineLength;
  }

  unsigned char* FramebufferBackend::getFBPointer()
  {
    return fbPointer + fbPointer_dist;
  }

  Mat& FramebufferBackend::getBackgroundBuff()
  {
    return backgroundBuff;
  }

  OpenCVFBMode FramebufferBackend::getMode()
  {
    return mode;
  }

  FramebufferBackend::FramebufferBackend():mode(FB_MODE_EMU), fbPointer_dist(0)
  {
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::FramebufferBackend()");
    
    std::string fbModeStr = getFBMode();
    
    if(fbModeStr == "EMU")
    {
      mode = FB_MODE_EMU;
      CV_LOG_WARNING(NULL, "UI: FramebufferWindow is trying to use EMU mode");
    }
    if(fbModeStr == "FB")
    {
      mode = FB_MODE_FB;
      CV_LOG_WARNING(NULL, "UI: FramebufferWindow is trying to use FB mode");
    }
    if(fbModeStr == "XVFB")
    {
      mode = FB_MODE_XVFB;
      CV_LOG_WARNING(NULL, "UI: FramebufferWindow is trying to use XVFB mode");
    }

    fbID = -1;
    if(mode == FB_MODE_FB)
    {
      fbID = fbOpenAndGetInfo();
    }
    if(mode == FB_MODE_XVFB)
    {
      fbID = XvfbOpenAndGetInfo();
    }
    
    CV_LOG_INFO(NULL, "UI: FramebufferWindow::fbID " << fbID);
  
    if(fbID == -1){
      mode = FB_MODE_EMU;
      fbWidth = 1024;
      fbHeight = 768;
      fbXOffset = 0;
      fbYOffset = 0;
      fbBitsPerPixel = 0;
      fbLineLength = 0;
      
      CV_LOG_WARNING(NULL, "UI: FramebufferWindow is used in EMU mode");
      return;
    }
    
    
    CV_LOG_INFO(NULL, "UI: Framebuffer's width, height, bits per pix: " 
      << fbWidth << " " << fbHeight << " " << fbBitsPerPixel);

    CV_LOG_INFO(NULL, "UI: Framebuffer's offsets (x, y), line length: " 
      << fbXOffset << " " << fbYOffset << " " << fbLineLength);
    

    backgroundBuff = Mat(fbHeight, fbWidth, CV_8UC4);
    int cnt_channel = 4;
    for (int y = fbYOffset; y < backgroundBuff.rows + fbYOffset; y++)
    {
      unsigned char* backgroundPtr = backgroundBuff.ptr<unsigned char>(y - fbYOffset);
      std::memcpy(backgroundPtr, 
                  fbPointer + y * fbLineLength + fbXOffset, 
                  backgroundBuff.cols * cnt_channel);
    }
  }
  
  FramebufferBackend::~FramebufferBackend()
  {
    CV_LOG_INFO(NULL, "UI: FramebufferBackend::~FramebufferBackend()");
    if(fbID == -1) return;
    
    // RESTORE BACKGROUNG
    if (fbPointer != MAP_FAILED) {

      int cnt_channel = 4;
      for (int y = fbYOffset; y < backgroundBuff.rows + fbYOffset; y++)
      {
        std::memcpy(fbPointer + y * fbLineLength + fbXOffset, 
                    backgroundBuff.ptr<cv::Vec4b>(y - fbYOffset), 
                    backgroundBuff.cols * cnt_channel);
      }

      munmap(fbPointer, fbScreenSize);
    }
    close(fbID);
  }

  void FramebufferBackend::destroyAllWindows() {
    CV_LOG_INFO(NULL, "UI: FramebufferBackend::destroyAllWindows()");
  }

  // namedWindow
  std::shared_ptr<UIWindow> FramebufferBackend::createWindow(
      const std::string& winname,
      int flags)
  {
    CV_LOG_INFO(NULL, "UI: FramebufferBackend::createWindow(" 
      << winname << ", " << flags << ")");
    return std::make_shared<FramebufferWindow>(*this, flags);
  }

  void FramebufferBackend::initTermios(int echo, int wait) 
  {
    tcgetattr(0, &old);               // grab old terminal i/o settings
    current = old;                    // make new settings same as old settings
    current.c_lflag &= ~ICANON;       // disable buffered i/o 
    current.c_lflag &= ~ISIG;
    current.c_cc[VMIN]=wait;
    if (echo) {
        current.c_lflag |= ECHO;      // set echo mode
    } else {
        current.c_lflag &= ~ECHO;     // set no echo mode
    }
    tcsetattr(0, TCSANOW, &current);  // use these new terminal i/o settings now
  }

  void FramebufferBackend::resetTermios(void) 
  {
    tcsetattr(0, TCSANOW, &old);
  }

  int FramebufferBackend::getch_(int echo, int wait) 
  {
    int ch;
    initTermios(echo, wait);
    ch = getchar();
    if(ch < 0) rewind(stdin);
    resetTermios();
    return ch;
  }
  
  bool FramebufferBackend::kbhit()
  {
    int byteswaiting=0;
    initTermios(0, 1);
    if ( ioctl(0, FIONREAD, &byteswaiting) < 0)
    {
      CV_LOG_ERROR(NULL, "UI: Framebuffer ERR byteswaiting" );
    }
    resetTermios();
    
    return byteswaiting > 0;
  }

  int FramebufferBackend::waitKeyEx(int delay) 
  {
    CV_LOG_INFO(NULL, "UI: FramebufferBackend::waitKeyEx(int delay = " << delay << ")");

    int code = -1;

    if(delay <= 0)
    {
      int ch = getch_(0, 1);
      CV_LOG_INFO(NULL, "UI: FramebufferBackend::getch_() take value = " << (int)ch);
      code = ch;
      
      while((ch = getch_(0, 0))>=0)
      {
        CV_LOG_INFO(NULL, "UI: FramebufferBackend::getch_() take value = " 
          << (int)ch << " (additional code on <stdin>)");
        code = ch;
      }
    } 
    else 
    {
      bool f_kbhit = false;
      while(!(f_kbhit = kbhit()) && (delay > 0))
      {
        delay -= 1;
        usleep(1000);
      }          
      if(f_kbhit)
      {
        CV_LOG_INFO(NULL, "UI: FramebufferBackend kbhit is True ");

        int ch = getch_(0, 1);
        CV_LOG_INFO(NULL, "UI: FramebufferBackend::getch_() take value = " << (int)ch);
        code = ch;
        
        while((ch = getch_(0, 0))>=0)
        {
          CV_LOG_INFO(NULL, "UI: FramebufferBackend::getch_() take value = " 
            << (int)ch << " (additional code on <stdin>)");
          code = ch;
        }
      }
    }
    
    CV_LOG_INFO(NULL, "UI: FramebufferBackend::waitKeyEx() result code = " << code);
    return code; 
  }
  
  int FramebufferBackend::pollKey()
  {
    CV_LOG_INFO(NULL, "UI: FramebufferBackend::pollKey()");
    int code = -1;
    bool f_kbhit = false;
    f_kbhit = kbhit();

    if(f_kbhit)
    {
      CV_LOG_INFO(NULL, "UI: FramebufferBackend kbhit is True ");

      int ch = getch_(0, 1);
      CV_LOG_INFO(NULL, "UI: FramebufferBackend::getch_() take value = " << (int)ch);
      code = ch;
      
      while((ch = getch_(0, 0))>=0)
      {
        CV_LOG_INFO(NULL, "UI: FramebufferBackend::getch_() take value = " 
          << (int)ch << " (additional code on <stdin>)");
        code = ch;
      }
    }
    
    return code;
  }

}
}
