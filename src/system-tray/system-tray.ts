import bindings from 'bindings';

interface ErrorAction {
  error: string;
}
interface ClickAction {
  click: string;
}
interface ReadyAction {
  ready: string;
}
interface EndAction {
  end: string;
}

type Action = ErrorAction | ClickAction | ReadyAction | EndAction;

interface Native {
  putNotifyIcon(trayItem: MenuItem, callback: (action: Action) => void): void;
}

export interface MenuItem {
  icon?: Buffer;
  title: string;
  tooltip?: string;
  items?: PossibleMenuItem[];
}

export interface MenuItemWithClickListener extends MenuItem {
  id: string;
  onClick: () => void;
}

export type PossibleMenuItem = MenuItem | MenuItemWithClickListener;

export type RootMenuItem = PossibleMenuItem & {
  icon: Buffer;
}

function matchMenuItemId(items: PossibleMenuItem[], id: string): MenuItemWithClickListener {
  for (const item of items) {
    if ('id' in item && item.id === id) {
      return item;
    } else if (item.items != null) {
      const childMatch = matchMenuItemId(item.items, id);
      if (childMatch != null) {
        return childMatch;
      }
    }
  }
  return null;
}

export function putSystemTray(trayItem: RootMenuItem): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    const native = bindings('system_tray') as Native;
    native.putNotifyIcon(trayItem, action => {
      if ('ready' in action) {
        resolve();
      } else if ('error' in action) {
        reject(new Error(action.error));
      } else if ('end' in action) {
        const end = action.end;
      } else if ('click' in action) {
        const id = action.click;
        const menuItem = matchMenuItemId([trayItem], id);
        if (menuItem != null) {
          queueMicrotask(() => menuItem.onClick());
        }
      } else {
        console.error('system-tray', 'putSystemTray', 'invalid action from native', action);
      }
    });
  });
}
