# Creating images:

1. On Windows PC install Python 3.91 or later, find it here: https://www.python.org/downloads/
1. Create an folder e.g. ``C:\temp\images\``  
1. Copy your source .JPG images to the folder e.g. ``myimage.jpg``
1. Copy ``imgconvert.py`` located in this scripts folder to ``c:\temp\images\``
1. Start a Windows cmd prompt, and chage directory to the folder:  
   ```
   > cd C:\temp\images\
   ```
1. To create an image for display, enter this at the command prompt:  
   ```
   > python imgconvert.py -i myimage.jpg - n myimage -o myimage.h
   ```
1. The image file is now created, move the ``myimage.h`` file into your sketch folder and include the file in your code like this 
   ```
   #include "myimage.h"
   ```
1. Draw the image like this:  
    ```
    DrawMyImage(x, y);

    void DrawMyImage(int x, int y) {   
        Rect_t area = { .x = x, .y = y, .width = 100, .height = 100 };   
        epd_draw_grayscale_image(area, (uint8_t *) myimage_data); 
    }
    ```

# Creating fonts:

1. On Windows PC install Python 3.91 or later, find it here: https://www.python.org/downloads/

1. Find a suitable Font Family e.g google opensans.ttf  
   [https://fonts.google.com/specimen/Open+Sans](https://fonts.google.com/specimen/Open+Sans)

1. Download the Font Family file

1. Create an accessible folder e.g. ``C:\temp\fonts\``

1. Unzip all *.ttf files to the folder. It now has all types of font files.ttf of the chosen font, like:  
  ``OpenSans-Bold.ttf``  
  ``OpenSans-BoldItalic.ttf``  
  ``OpenSans-Regular.ttf``  
  ``...``  

1. Copy ``fontconvert.py`` located in this scripts folder to ``c:\temp\fonts\``

1. Start a Windows cmd prompt, and chage directory to the folder:  
   ```
   > cd C:\temp\fonts\
   ```

1. Create fonts you need.  
   ```
   To create a Regular 10-point font, enter this at the command prompt:  
   > python fontconvert.py OpenSans12 12 OpenSans-Regular.ttf > opensans12.h

   To create a Bold 10-point font, enter this at the command prompt:  
   > python fontconvert.py OpenSans10B 10 OpenSans-Bold.ttf > opensans10b.h
   ```  
1. The font files are now created. Move the ``opensansNNx.h`` file into your sketch folder and include the file in your code like this 
   ```
   #include "opensans10b.h"
   ```
