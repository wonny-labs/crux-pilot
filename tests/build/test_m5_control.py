"""Actual private C control: reject commands before any traffic can be selected."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class FixtureControl(unittest.TestCase):
    def test_only_armed_sequenced_bounded_commands_can_execute(self):
        # Break caught: accepting replay/wrong nonce/overflow/count or mutating on rejection.
        code = r'''
#include "control.h"
#include <stdio.h>
#include <string.h>
int main(void) {
 struct fixture_session s={0};struct fixture_command c={0};char line[160];
 while(fgets(line,sizeof(line),stdin)) {
  line[strcspn(line,"\r\n")]=0;
  s.ticket=99;
  int r=fixture_control(line,&s,&c);
  printf("%d %u %u %u",r,s.token,s.next_sequence,s.pairs);
  if(!r)printf(" %c %u %u",c.action,c.sequence,c.count);
  puts("");
 }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'test.c';binary=Path(tmp)/'test';source.write_text(code)
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/networking_physical'),str(source),str(ROOT/'tests/networking_physical/control.c'),'-o',str(binary)],check=True)
            lines=['P 7 1 1','A 0','A 7','A 8','P 8 1 1','P 7 2 1','P 7 1 0','P 7 1 11','P 7 1 10x','P 7 1 10','P 7 1 10','I 7 2','L 7 3','P 7 4 10','P 7 5 10','P 7 6 10','P 7 7 10','P 7 7 9','P 7 8 1']
            r=subprocess.run([str(binary)],input='\n'.join(line if line.startswith('A ') else line+' 99' for line in lines)+'\n',text=True,capture_output=True,check=True)
            self.assertEqual(r.stdout.splitlines(),[
                '-1 0 0 0','-1 0 0 0','0 7 1 0 A 0 0','-1 7 1 0',
                '-1 7 1 0','-1 7 1 0','-1 7 1 0','-1 7 1 0','-1 7 1 0',
                '0 7 2 10 P 1 10','-1 7 2 10','0 7 3 10 I 2 0',
                '0 7 4 11 L 3 1','0 7 5 21 P 4 10','0 7 6 31 P 5 10',
                '0 7 7 41 P 6 10','-1 7 7 41','0 7 8 50 P 7 9','-1 7 8 50'])

    def test_readiness_ticket_rejects_queued_future_command(self):
        code=r'''#include "control.h"
int main(void){struct fixture_session s={0};struct fixture_command c={0};
 if(fixture_control("A 7",&s,&c))return 1;
 s.ticket=11;
 if(fixture_control("P 7 1 1 11",&s,&c))return 2;
 if(!fixture_control("P 7 2 1 11",&s,&c))return 3;
 s.ticket=12;
 if(!fixture_control("P 7 2 1 11",&s,&c))return 4;
 if(fixture_control("P 7 2 1 12",&s,&c))return 5;
 return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'ticket.c';binary=Path(tmp)/'ticket';source.write_text(code)
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/networking_physical'),str(source),str(ROOT/'tests/networking_physical/control.c'),'-o',str(binary)],check=True)
            self.assertEqual(subprocess.run([str(binary)]).returncode,0)

if __name__=='__main__':unittest.main()
