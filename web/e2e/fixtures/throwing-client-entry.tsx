// The reference with a client whose cookie reader can be made to throw, to show that a call that rejects
// does not leave the configuration screen locked. The client itself never throws for an HTTP or network
// failure, so this is the only way a request can reject: a reader that a project supplied. A test sets
// `cruxTestThrows` to the number of reads that should throw.
import '../../reference/src/style.css';
import { mountReferenceInterface } from '../../reference/src/app.tsx';
import { configScreen } from '../../reference/src/screens/config.tsx';
import { homeScreen } from '../../reference/src/screens/home.tsx';

const hooks = globalThis as unknown as { cruxTestThrows?: number };

const root = document.getElementById('app');
if (root !== null) {
  mountReferenceInterface(root, {
    screens: [homeScreen, configScreen],
    client: {
      readCookies: () => {
        if ((hooks.cruxTestThrows ?? 0) > 0) {
          hooks.cruxTestThrows = (hooks.cruxTestThrows ?? 0) - 1;
          throw new Error('the cookie reader failed');
        }
        return document.cookie;
      },
    },
  });
}
