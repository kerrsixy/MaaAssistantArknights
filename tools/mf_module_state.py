import sys, json
import cv2, numpy as np

ROI = (1050, 515, 200, 110)   # 模组按钮


def find_lock(img, template):
    res = cv2.matchTemplate(img, template, cv2.TM_CCOEFF_NORMED)
    return float(res.max())


def main():
    shot = sys.argv[1]
    tmpl = sys.argv[2]
    img = cv2.imread(shot)
    x, y, w, h = ROI
    roi = img[y:y + h, x:x + w]
    t = cv2.imread(tmpl, cv2.IMREAD_GRAYSCALE)
    g = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    best = 0.0
    for s in np.arange(0.9, 1.15, 0.05):
        tt = cv2.resize(t, None, fx=s, fy=s)
        if tt.shape[0] > g.shape[0] or tt.shape[1] > g.shape[1]:
            continue
        best = max(best, find_lock(g, tt))
    print(json.dumps({"lock_score": round(best, 4), "locked": best >= 0.90}, ensure_ascii=False))


if __name__ == "__main__":
    main()