project=mousecounter

all: $(project)

$(project): $(project).c
	cc -lpthread $< -o $@

clean:
	rm -f $(project) $(project).o *~
