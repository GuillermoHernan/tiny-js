// test for array contains
var a = [1,2,4,5,7];
var b = ["bread","cheese","sandwich"];

result = a.indexOf(1) == 0 && a.indexOf(5) == 3 && a.indexOf(42) < 0 
        && b.indexOf("cheese")==1 && b.indexOf("sandwich")==2 && b.indexOf("eggs") < 0;
