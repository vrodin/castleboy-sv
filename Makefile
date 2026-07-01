# CastleBoy - Watara Supervision port (cc65)
# CastleBoy (c) 2016 dir3kt/jlauener, MIT License (see LICENSE.castleboy).

SYS ?= supervision
CL := $(shell command -v cl65 2>/dev/null || echo cl65)
NAME = castleboy
SRCS = main.c assets.c assets_preshift.c game.c player.c entity.c map.c platform.c

# The optimizations + pre-shifted player push past 32 KB, so the canonical build
# is now the 64 KB banked cart. `make 32k` still exists but no longer fits.
all: 64k

# Asset generators live in python/. gen_assets.py reads the upstream CastleBoy
# assets.h (override its location with CASTLEBOY_ASSETS) and writes assets.h/.c
# into the repo root; gen_preshift.py derives the pre-shifted sheets from that.
assets.h assets.c: python/gen_assets.py
	python3 python/gen_assets.py

# pre-shifted enemy sprites + (64k) player bank are derived from assets.c
assets_preshift.h assets_preshift.c assets_player_ps.c: python/gen_preshift.py assets.c
	python3 python/gen_preshift.py

# -O optimize; --register-vars maps `register` locals into the zero-page
# register bank (faster pointer-heavy inner loops on the 65C02).
CFLAGS = -O --register-vars

# legacy 32KB build (no banking; a few bytes over the cart after the blit/
# save-under optimizations - kept for reference)
32k: $(NAME).sv
$(NAME).sv: $(SRCS) castleboy.h assets.h assets_preshift.h
	$(CL) -t $(SYS) $(CFLAGS) -o $(NAME).sv -m $(NAME).map $(SRCS)

# 64KB banked build: pre-shifted player in ROM bank 1, code in fixed C000-FFFF.
# crt0.s is our own startup (selects ROM bank 0 at reset) and is listed first so
# it overrides cc65's stock supervision crt0 - no post-link patch needed.
SRCS64 = crt0.s $(SRCS) assets_player_ps.c bankblit.s
64k: $(NAME)-64k.sv
$(NAME)-64k.sv: $(SRCS64) castleboy.h assets.h assets_preshift.h castleboy-64k.cfg
	$(CL) -t $(SYS) $(CFLAGS) -DCB_BANKED -C castleboy-64k.cfg \
	      -o $(NAME)-64k.sv -m $(NAME)-64k.map $(SRCS64)

clean:
	$(RM) $(NAME).sv $(NAME).map $(NAME)-64k.sv $(NAME)-64k.map *.o

start: $(NAME)-64k.sv
	retroarch -L ~/.config/retroarch/cores/potator_libretro.so $(NAME)-64k.sv

.PHONY: all 64k 32k clean start
