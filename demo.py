import cv2 as cv
import numpy as np

# Đọc ảnh gốc
img = cv.imread('real.jpg', cv.IMREAD_COLOR)

# Lấy kích thước ảnh
h, w, c = img.shape  

# Tạo ảnh rỗng (đen)
out = np.zeros((h, w, 3), dtype=np.uint8)

# Hiển thị ảnh từng dòng từ trên xuống
for j in range(w):
    out[j, :, :] = img[j, :, :]  # Copy từng dòng từ img vào out
    cv.imshow('Loading Image...', out)  # Hiển thị ảnh từng bước
    cv.waitKey(10)  # Dừng 10ms để quan sát quá trình tải ảnh

# Dừng chương trình khi người dùng nhấn phím bất kỳ
cv.waitKey(0)
cv.destroyAllWindows()
