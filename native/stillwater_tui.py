"""A dependency-free curses interface. No background work after it closes."""
import curses

LABELS = {'pond': 'Koi pond', 'sky': 'Night sky', 'mountains': 'Mountains',
          'forest': 'Firefly forest', 'rain': 'Rain garden', 'dunes': 'Quiet dunes'}
DESCRIPTIONS = {
    'pond': 'Copper koi, lily pads, and circles in the water.',
    'sky': 'A crescent moon, twinkling stars, and passing meteors.',
    'mountains': 'Layered peaks, slow mist, and a moonlit lake.',
    'forest': 'Evergreen silhouettes and wandering fireflies.',
    'rain': 'Bamboo, drifting rain, and ripples in the garden.',
    'dunes': 'Warm sand, a low sun, and wind across the ridges.',
}


def run(controller, scenes):
    def interface(screen):
        try:
            curses.curs_set(0)
        except curses.error:
            pass
        screen.keypad(True)
        screen.timeout(1000)
        accent, muted = curses.A_BOLD, curses.A_NORMAL
        if curses.has_colors():
            curses.start_color()
            try:
                curses.use_default_colors()
                curses.init_pair(1, curses.COLOR_CYAN, -1)
                accent = curses.color_pair(1) | curses.A_BOLD
            except curses.error:
                pass
        status = controller.status()
        draft = status['settings'].copy()
        selected = 0
        message = 'Choose a setting, then use Left / Right. Enter saves and applies.'
        fields = ('scene', 'fps', 'speed')
        fps_choices = (1, 4, 6, 8, 12, 15, 20, 24, 30)
        speeds = (.25, .5, .75, 1., 1.25, 1.5, 2.)

        def change(step):
            field = fields[selected]
            options = scenes if field == 'scene' else fps_choices if field == 'fps' else speeds
            value = draft[field]
            if value in options:
                index = options.index(value)
            else:
                index = min(range(len(options)), key=lambda i: abs(options[i] - value))
            draft[field] = options[(index + step) % len(options)]

        while True:
            screen.erase()
            height, width = screen.getmaxyx()

            def text(y, x, value, attribute=0):
                if 0 <= y < height and x < width - 1:
                    try:
                        screen.addnstr(y, max(0, x), value, max(0, width - x - 1), attribute)
                    except curses.error:
                        pass

            if height < 19 or width < 48:
                text(1, 2, 'STILLWATER', accent)
                text(3, 2, 'Resize to at least 48 columns x 19 rows.')
                text(5, 2, 'Q closes without changing your wallpaper.')
            else:
                record = status['process']
                state = 'Paused' if record.get('paused') else 'Playing' if record else 'Static wallpaper'
                text(1, 3, 'STILLWATER', accent)
                text(1, max(22, width - len(state) - 4), state)
                text(3, 3, 'A little movement. A little room to breathe.', muted)
                text(5, 3, 'Wallpaper settings', accent)
                values = [LABELS[draft['scene']], str(draft['fps']) + ' fps', f'{draft["speed"]:g}x speed']
                names = ['Landscape', 'Frame rate', 'Animation pace']
                for index, (name, value) in enumerate(zip(names, values)):
                    marker = '>' if index == selected else ' '
                    text(7 + index * 2, 3, f'{marker} {name:<18} < {value} >', accent if index == selected else 0)
                if height >= 25:
                    text(16, 5, DESCRIPTIONS[draft['scene']])
                    text(19, 5, 'At login: ' + ('enabled' if status['settings']['enabled'] else 'disabled'))
                if height >= 25 and width >= 88:
                    x = 55
                    for index, name in enumerate(scenes):
                        text(7 + index, x, ('* ' if name == draft['scene'] else '  ') + LABELS[name], accent if name == draft['scene'] else 0)
                text(height - 4, 3, message)
                text(height - 2, 3, 'Up/Down select  Left/Right edit  Enter apply')
                text(height - 1, 3, 'S enable  P pause  X static/off  Q quit')
            screen.refresh()
            key = screen.getch()
            if key in (ord('q'), ord('Q'), 27):
                return
            if height < 19 or width < 48:
                continue
            if key in (curses.KEY_DOWN, ord('j'), 9):
                selected = (selected + 1) % len(fields)
            elif key in (curses.KEY_UP, ord('k')):
                selected = (selected - 1) % len(fields)
            elif key in (curses.KEY_RIGHT, ord('l')):
                change(1)
            elif key in (curses.KEY_LEFT, ord('h')):
                change(-1)
            else:
                try:
                    if key in (10, 13, curses.KEY_ENTER):
                        message = controller.update({field: draft[field] for field in fields})
                    elif key in (ord('s'), ord('S')):
                        message = controller.update({field: draft[field] for field in fields}, activate=True)
                    elif key in (ord('p'), ord('P'), ord(' ')):
                        message = controller.action('pause')
                    elif key in (ord('x'), ord('X')):
                        message = controller.action('disable')
                except (OSError, ValueError) as error:
                    message = str(error)
            try:
                status = controller.status()
            except (OSError, ValueError) as error:
                message = str(error)
    try:
        curses.wrapper(interface)
    except curses.error as error:
        raise ValueError('Could not open the terminal interface. Check TERM or use stillwater --help.') from error
