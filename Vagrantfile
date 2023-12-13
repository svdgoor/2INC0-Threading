Vagrant.configure("2") do |config|
  config.vm.box = "bento/ubuntu-20.04"

  config.vm.provider "virtualbox" do |vb|
  vb.cpus = 2
  vb.memory = 4096
  
  end

  # If you comment the line above, use second commented line
  # config.vm.network "private_network", ip: "192.168.56.20", name: "HostOnly", virtualbox__intnet: true
  config.vm.network "private_network", ip: "192.168.56.20"

  config.vm.synced_folder ".", "/home/vagrant/code"

  config.vm.provision "shell", path: "setup.sh", privileged: false, keep_color: true
end
