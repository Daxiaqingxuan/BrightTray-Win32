"""生成内嵌图标数据 (RT_ICON 格式的 DIB)，输出 icon_data.h"""
import struct, math

def render(size, samples=6):
    """超采样绘制太阳，返回 (w,h,bgra rows bottom-up)"""
    cx = cy = size / 2.0
    radius = size * 0.176          # 实心圆半径
    r_in = size * 0.281            # 射线内端
    r_out = size * 0.4375          # 射线外端
    thick = max(1.6, size * 0.078)  # 射线粗细(半径)
    px = []
    for y in range(size):
        row = []
        for x in range(size):
            hit = 0
            for sy in range(samples):
                for sx in range(samples):
                    fx = x + (sx + 0.5) / samples - cx
                    fy = y + (sy + 0.5) / samples - cy
                    d = math.hypot(fx, fy)
                    inside = d <= radius
                    if not inside and r_in <= d <= r_out:
                        a = math.atan2(fy, fx)
                        # 8 条射线, 判断到最近射线中心线的角距离
                        step = math.pi / 4
                        da = abs(((a + step / 2) % step) - step / 2)
                        if abs(d * math.sin(da)) <= thick / 2:
                            inside = True
                    hit += 1 if inside else 0
            a = hit / (samples * samples)
            row.append(a)
        px.append(row)
    return px

def to_dib(alpha_rows):
    h = len(alpha_rows)
    w = len(alpha_rows[0])
    mask_row = (w + 31) // 32 * 4
    # XOR bitmap, bottom-up
    xor = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            a = alpha_rows[y][x]
            # 颜色 #FFB900 带轻微高光
            r, g, b = 255, 185, 0
            if a > 0.55:
                t = (a - 0.55) / 0.45
                r = int(255 * (1 - 0.08 * t))
                g = int(185 * (1 - 0.10 * t))
                b = int(0)
            xor += bytes((b, g, r, int(round(a * 255))))
    # AND mask, bottom-up, 1 = transparent
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        rowbits = bytearray(mask_row)
        for x in range(w):
            if alpha_rows[y][x] < 0.5:
                rowbits[x // 8] |= 0x80 >> (x % 8)
        mask += rowbits
    bi = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, len(xor), 0, 0, 0, 0)
    return bytes(bi) + bytes(xor) + bytes(mask)

def c_array(name, data):
    lines = ['static const unsigned char %s[] = {' % name]
    for i in range(0, len(data), 16):
        lines.append('    ' + ','.join('0x%02x' % b for b in data[i:i + 16]) + ',')
    lines.append('};')
    return '\n'.join(lines)

out = ['/* 自动生成：太阳图标 (32px / 16px, 带 alpha 抗锯齿) */',
       '#ifndef ICON_DATA_H', '#define ICON_DATA_H', '']
for size, name in ((32, 'k_icon32'), (16, 'k_icon16')):
    out.append(c_array(name, to_dib(render(size))))
    out.append('')
out.append('#endif')
open('icon_data.h', 'w', newline='\n').write('\n'.join(out) + '\n')
print('icon_data.h written')
