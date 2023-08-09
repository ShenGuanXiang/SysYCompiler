#include<iostream>
using namespace std;
int main(){
    int mod = 2147385347;
    int sum = 0;
    //[0...1048575)%mod
    for(int i =0;i<1048575;i++){
        sum = sum + i;
        cout << sum << endl;
        sum = sum % mod;
    }
    cout<<sum<<endl;
    cout<<1048574*1048575/2%mod<<endl;
}