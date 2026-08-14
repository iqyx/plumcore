import "painter" for Painter
import "fb-painter" for FbPainter

args.logger.info("hello from the Wren applet '%(args.name)'")
args.logger.info("help: %(args.help)")
args.logger.info("framebuffer is %(args.fb.width)x%(args.fb.height), mode %(args.fb.mode)")

var fbp = FbPainter.new(args.fb)
var p = fbp.painter
var w = args.fb.width
var h = args.fb.height

p.begin()
// Clear the whole surface to black.
p.setPen(0xff000000, 1)
p.setBrush(0xff000000)
p.rect(0, 0, w, h)
// A white-bordered, black-filled rectangle near the top.
p.setPen(0xffffffff, 1)
p.setBrush(0xff000000)
p.rect(8, 8, w - 16, 32)
// A caption centred inside the rectangle.
p.setFont(Painter.normal)
var text = "Hello from Wren!"
var tx = 8 + (w - 16 - p.textWidth(text)) / 2
var ty = 8 + (32 - p.textHeight(text)) / 2
p.text(tx, ty, text)
p.end()

args.logger.info("drawn, waiting for ESC")

// Block on the event source until ESC is pressed.
while (args.event.wait()) {
	if (args.event.type == Event.EV_TYPE_KEY && args.event.value == 1 && args.event.code == Event.EV_KEY_ESC) {
		break
	}
}
args.logger.info("ESC pressed, exiting")
