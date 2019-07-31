log_capture
=========

Overview
--------

log_capture is a package of tool used to collect various logs from linux kernel and AOSP. It includes a tool - crashlogd, and libraries used by the tool.
Crashlogd is an daemon running on the target to collect device bad behaviors (crashes, errors...), system info, device info and some statistics. It is running
in the background, waiting for new crash events to occurs, once new event occurs, crashlogd will leverage different mechanism to collect all related crash
information and store to one unique folder, recorded in history_event as a crash event, developer can use history_event file to check each crash and resolve
crash with collected crashlog 


Contributors
------------
Commit message was not upstreamed due to there have confidential information in this project.
Confidential information was removed and upstreamed as a whole project.

list the contributors for the previous commits:  
Tian Baofeng  
Chen, Xiao X  
zhouji3x  
Zhang Dongxing  
Christophe Guerard  
Michel Jauffres  
Vidoine, Charles-EdouardX  
Benoit, NicolasX  
Christophe Lebouc  
Jeremy Rocher  
Guobin Zhang  
Morgane BUTSCHER  
Vidoine, Charles-EdouardX  
Jacques Imougar  
Jean-Christophe PINCE  
Thiry, JeanX  
David Castelain  
Leon Ma  
Laurent JOSSE  
Fengwei Yin  
Chetan Rao  
Lab  
rkraiemx  
jzha144  
Lecat, AurelienX  
Yann Fouassier  
Sebbane, AdrienX  
Flavien Boutoille  
Haithem Salah  
Cesar DE OLIVEIRA  
Florent Auger  
Guillaume Lucas  
Guilhem IMBERTON  
Imberton, Guilhem  
Berthe, AbdoulayeX  
Lionel Ulmer  
wang, biao  
François Trebosc  
Claudia Bibu  
Traian Schiau  
Nicolae Natea  
Marc Mao  
Laurent FERT  
Juan Antonio Gozalvez Herrero  
Clement Calmels  
Haithem Salah  
Olivier Fourdan  
Vasile Iamandi  
Jinsog Bu  
Axel Fagerstedt  
Nicolas Heckmann  
Sandeep Kumar  
JesslynX A Salam  
Basanagouda Koppad  
Ravi Chandra G  
Qiming Shi  
Chen Yu Y  
shankun  
jialei.feng  
Tang Guifang  
zhang jun  
geoffroy.weisenhorn  
Axel Fagerstedt  
Andreea Beza
