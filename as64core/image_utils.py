import numpy as np
import cv2


def is_black(image, threshold=0.1, percent_threshold=0.9):
    img_1d = convert_to_np([image]).flatten()
    return np.sum(img_1d < threshold) > len(img_1d) * percent_threshold


def is_white(image, threshold=0.8):
    img_1d = convert_to_np([image]).flatten()
    return np.sum(img_1d > threshold) > len(img_1d) * 0.99


def convert_to_np(img_array):
    # Convert to numpy array and normalize
    np_img = np.array(img_array).astype(np.float32)
    np_img = np_img / 255.0
    
    # Add batch dimension
    np_img = np.expand_dims(np_img, axis=0)
    return np_img


def convert_to_cv2(img):
    open_cv_image = np.array(img)
    # Convert RGB to BGR
    return open_cv_image[:, :, ::-1].copy()


def cv2_convert_to_gray(img):
    return cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)


def np_convert_to_gray(np_rgb):
    return np.dot(np_rgb[..., :3], [0.299, 0.587, 0.114])

def enhance_contrast(image, contrast_factor=0.85):
    
    # Convert to float for calculations
    img_float = image.astype(float)
    
    # Calculate mean brightness
    mean = np.mean(img_float)
    
    # Apply contrast adjustment
    enhanced = mean + (img_float - mean) * contrast_factor
    
    # Clip and convert back to uint8
    enhanced = np.clip(enhanced, 0, 255).astype(np.uint8)
    
    return enhanced