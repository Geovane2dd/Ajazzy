/*
 * The Ajazzy visual language, in one place.
 *
 * Built on libadwaita's own named colours (window_bg_color, card_bg_color,
 * window_fg_color...) instead of a fixed palette, so the same sheet works in
 * light and dark and follows the user's choice in Settings. Only the brand
 * accent is overridden. Everything custom is prefixed `aj-`.
 *
 * Scale: 4/8/12/16/24/32 spacing, 8/12/16 radii, 12/13/15/24 type sizes.
 */
#include "ui.h"

const char *AJAZZ_CSS =
    /* ---- brand ---- */
    "@define-color accent_bg_color #e8394a;"
    "@define-color accent_fg_color #ffffff;"
    "@define-color accent_color #d02a3b;"
    "@media (prefers-color-scheme: dark) { @define-color accent_color #ff6674; }"

    /* ---- page ---- */
    ".aj-page-title { font-size: 24px; font-weight: 800; letter-spacing: -0.3px; }"
    ".aj-page-sub { font-size: 13px; opacity: 0.62; }"
    ".aj-section { font-size: 11px; font-weight: 800; letter-spacing: 1.2px;"
    "  text-transform: uppercase; opacity: 0.5; margin: 10px 4px 0 4px; }"

    /* ---- card ---- */
    ".aj-card { background-color: @card_bg_color; border-radius: 14px;"
    "  border: 1px solid alpha(currentColor, 0.09);"
    "  box-shadow: 0 1px 2px alpha(black, 0.12); }"
    ".aj-card-head { padding: 16px 18px 8px 18px; }"
    ".aj-card-title { font-size: 15px; font-weight: 700; }"
    ".aj-card-desc { font-size: 12px; opacity: 0.6; }"
    ".aj-card-pad { padding: 8px 18px 18px 18px; }"
    ".aj-card.aj-flush .aj-card-pad { padding: 0; }"

    /* ---- list rows inside cards ---- */
    "list.aj-list { background: transparent; padding: 4px 6px 8px 6px; }"
    "list.aj-list > row { background: transparent; border-radius: 10px;"
    "  min-height: 52px; margin: 1px 0; }"
    "list.aj-list > row:hover { background-color: alpha(currentColor, 0.04); }"
    "list.aj-list > row .title { font-weight: 600; font-size: 13px; }"
    "list.aj-list > row .subtitle { font-size: 12px; opacity: 0.6; }"
    "list.aj-list > row.activatable:active { background-color: alpha(currentColor, 0.08); }"

    /* ---- buttons ---- */
    "button.aj-btn { border-radius: 10px; min-height: 36px; padding: 0 16px;"
    "  font-weight: 600; transition: all 120ms ease; box-shadow: none; }"
    "button.aj-btn-primary { background: @accent_bg_color; color: @accent_fg_color; }"
    "button.aj-btn-primary:hover { background: shade(@accent_bg_color, 1.1);"
    "  box-shadow: 0 4px 16px alpha(@accent_bg_color, 0.38); }"
    "button.aj-btn-primary:disabled { opacity: 0.45; }"
    "button.aj-btn-secondary { background: alpha(currentColor, 0.08); }"
    "button.aj-btn-secondary:hover { background: alpha(currentColor, 0.14); }"
    "button.aj-btn-ghost { background: transparent; }"
    "button.aj-btn-ghost:hover { background: alpha(currentColor, 0.08); }"
    "button.aj-btn-danger { background: alpha(#e5484d, 0.14); color: #e5484d; }"
    "button.aj-btn-danger:hover { background: #e5484d; color: white; }"

    /* ---- segmented control ---- */
    ".aj-seg > button { min-height: 34px; padding: 0 14px; font-weight: 600;"
    "  background: alpha(currentColor, 0.07); box-shadow: none; }"
    ".aj-seg > button:checked { background: @accent_bg_color; color: @accent_fg_color; }"
    ".aj-seg > button:hover:not(:checked) { background: alpha(currentColor, 0.13); }"

    /* ---- chips ---- */
    "label.aj-chip { border-radius: 999px; padding: 3px 11px; font-size: 11px; font-weight: 700;"
    "  letter-spacing: 0.2px; }"
    "label.aj-chip-neutral { background: alpha(currentColor, 0.09); }"
    "label.aj-chip-ok { background: alpha(#2fb872, 0.16); color: #2fb872; }"
    "label.aj-chip-warn { background: alpha(#e8a317, 0.17); color: #e8a317; }"
    "label.aj-chip-error { background: alpha(#e5484d, 0.16); color: #e5484d; }"
    "label.aj-chip-accent { background: alpha(@accent_bg_color, 0.17); color: @accent_color; }"

    /* ---- status dot ---- */
    ".aj-dot { min-width: 9px; min-height: 9px; border-radius: 999px; }"
    ".aj-dot-on { background: #2fb872; box-shadow: 0 0 8px alpha(#2fb872, 0.8); }"
    ".aj-dot-off { background: #e5484d; }"
    ".aj-dot-busy { background: #e8a317; }"

    /* ---- sidebar ---- */
    ".aj-sidebar { background-color: @sidebar_bg_color; }"
    ".aj-brand { padding: 20px 18px 14px 18px; }"
    ".aj-brand-name { font-size: 17px; font-weight: 900; letter-spacing: 2.5px; }"
    ".aj-brand-sub { font-size: 10px; font-weight: 700; letter-spacing: 0.8px;"
    "  text-transform: uppercase; opacity: 0.45; }"
    "list.aj-nav { background: transparent; padding: 0 10px; }"
    "list.aj-nav > row { border-radius: 10px; margin: 2px 0; min-height: 40px; padding: 0; }"
    "list.aj-nav > row:hover { background-color: alpha(currentColor, 0.06); }"
    "list.aj-nav > row:selected { background-color: alpha(@accent_bg_color, 0.16); }"
    "list.aj-nav > row:selected label { font-weight: 700; }"
    "list.aj-nav > row:selected image { color: @accent_color; }"
    "list.aj-nav > row image { opacity: 0.75; }"
    ".aj-nav-group { font-size: 10px; font-weight: 800; letter-spacing: 1.2px;"
    "  text-transform: uppercase; opacity: 0.4; margin: 16px 20px 4px 20px; }"
    ".aj-side-foot { padding: 12px 18px 16px 18px; border-top: 1px solid alpha(currentColor, 0.08); }"

    /* ---- hero ---- */
    ".aj-hero { border-radius: 16px; padding: 22px 24px;"
    "  background-image: linear-gradient(120deg, alpha(@accent_bg_color, 0.22), alpha(@accent_bg_color, 0.03) 65%);"
    "  border: 1px solid alpha(@accent_bg_color, 0.28); }"
    ".aj-hero-name { font-size: 22px; font-weight: 900; letter-spacing: -0.3px; }"
    ".aj-hero-meta { font-size: 12px; opacity: 0.65; }"
    ".aj-mouse-badge { background: alpha(currentColor, 0.07); border-radius: 18px;"
    "  padding: 10px; }"

    /* ---- stat tile (clickable summary on Overview) ---- */
    "button.aj-tile { background-color: @card_bg_color; border-radius: 14px; padding: 16px 18px;"
    "  border: 1px solid alpha(currentColor, 0.09); box-shadow: 0 1px 2px alpha(black, 0.12);"
    "  transition: all 140ms ease; }"
    "button.aj-tile:hover { border-color: alpha(@accent_bg_color, 0.55);"
    "  box-shadow: 0 4px 18px alpha(@accent_bg_color, 0.16); }"
    ".aj-tile-label { font-size: 11px; font-weight: 700; letter-spacing: 0.8px;"
    "  text-transform: uppercase; opacity: 0.55; }"
    ".aj-tile-value { font-size: 26px; font-weight: 800; letter-spacing: -0.5px; }"
    ".aj-tile-sub { font-size: 12px; opacity: 0.6; }"

    /* ---- DPI stage selector ---- */
    "button.aj-stage { border-radius: 12px; padding: 12px 6px; min-width: 88px;"
    "  background: alpha(currentColor, 0.06); border: 2px solid transparent;"
    "  box-shadow: none; transition: all 120ms ease; }"
    "button.aj-stage:hover { background: alpha(currentColor, 0.11); }"
    "button.aj-stage:checked { border-color: @accent_bg_color; background: alpha(@accent_bg_color, 0.12); }"
    ".aj-stage-dot { min-width: 12px; min-height: 12px; border-radius: 999px; }"
    ".aj-stage-num { font-size: 11px; font-weight: 700; opacity: 0.6; }"
    ".aj-stage-val { font-size: 17px; font-weight: 800; }"

    /* ---- effect gallery ---- */
    "button.aj-effect { border-radius: 14px; padding: 0; background: alpha(currentColor, 0.05);"
    "  border: 2px solid transparent; box-shadow: none; transition: all 140ms ease; }"
    "button.aj-effect:hover { background: alpha(currentColor, 0.1); }"
    "button.aj-effect:checked { border-color: @accent_bg_color; background: alpha(@accent_bg_color, 0.1); }"
    ".aj-effect-name { font-weight: 700; font-size: 13px; }"
    ".aj-effect-note { font-size: 11px; opacity: 0.55; }"

    /* ---- button-mapping diagram markers ---- */
    "button.aj-marker { min-width: 30px; min-height: 30px; padding: 0; border-radius: 999px;"
    "  font-size: 12px; font-weight: 800; background: alpha(black, 0.55); color: white;"
    "  border: 1.5px solid alpha(white, 0.45); box-shadow: none; }"
    "button.aj-marker:hover { border-color: white; }"
    "button.aj-marker:checked { background: @accent_bg_color; border-color: white;"
    "  box-shadow: 0 0 0 4px alpha(@accent_bg_color, 0.35); }"

    /* ---- product photo behind the button markers: always a dark stage, since the
     * photos are white mice and would vanish on a light card ---- */
    ".aj-photo { background-image: radial-gradient(ellipse at 50% 35%, #3a3d46 0%, #1b1c21 75%);"
    "  border-radius: 12px; }"

    /* ---- function picker ---- */
    "button.aj-func { border-radius: 10px; padding: 8px 12px; background: alpha(currentColor, 0.05);"
    "  border: 1.5px solid transparent; box-shadow: none; font-weight: 600; }"
    "button.aj-func:hover { background: alpha(currentColor, 0.1); }"
    "button.aj-func:checked { border-color: @accent_bg_color; background: alpha(@accent_bg_color, 0.13); }"

    /* ---- macro editor ---- */
    ".aj-event { border-radius: 10px; padding: 6px 10px; background: alpha(currentColor, 0.05); }"
    ".aj-event-down { border-left: 3px solid #2fb872; }"
    ".aj-event-up { border-left: 3px solid #e8a317; }"
    ".aj-event-delay { border-left: 3px solid alpha(currentColor, 0.35); }"
    ".aj-rec { color: #e5484d; font-weight: 800; }"
    "button.aj-rec-on { background: #e5484d; color: white; }"

    /* ---- empty state ---- */
    ".aj-empty { padding: 36px 24px; }"
    ".aj-empty-icon { opacity: 0.4; }"
    ".aj-empty-title { font-size: 15px; font-weight: 700; }"
    ".aj-empty-hint { font-size: 12px; opacity: 0.6; }"

    /* ---- hint / callout ---- */
    ".aj-callout { border-radius: 10px; padding: 10px 14px; background: alpha(#e8a317, 0.12);"
    "  border: 1px solid alpha(#e8a317, 0.3); font-size: 12px; }"
    ".aj-callout-info { background: alpha(@accent_bg_color, 0.1); border-color: alpha(@accent_bg_color, 0.28); }"

    /* ---- toolbar-ish ---- */
    "headerbar { box-shadow: none; }";
