from PIL import Image

im = Image.open('plum_32_g2.png')

## @todo handle palette data and colors properly

print(f'const struct imdata im = {{')
print(f'\t{im.width},')
print(f'\t{im.height},')
print(f'\tFB_MODE_G2,')

print(f'\t{{')
imdata = list(im.getdata())
imdata2 = b'';
for y in range(im.height):
	linedata = 0;
	for x in range(im.width):
		linedata = linedata * 4 + imdata[y * im.width + x]

	linedata = linedata.to_bytes(int(im.width / 4))
	imdata2 += linedata

	print('\t\t' + ', '.join(['0x%02x' % b for b in linedata]) + ',')

print(f'\t}}')
print(f'}}')

